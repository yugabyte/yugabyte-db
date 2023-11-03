//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "yb/rocksdb/db/memtable.h"

#include <algorithm>
#include <limits>

#include "yb/rocksdb/db/dbformat.h"
#include "yb/rocksdb/db/merge_context.h"
#include "yb/rocksdb/env.h"
#include "yb/rocksdb/iterator.h"
#include "yb/rocksdb/merge_operator.h"
#include "yb/rocksdb/slice_transform.h"
#include "yb/rocksdb/table/internal_iterator.h"
#include "yb/rocksdb/util/arena.h"
#include "yb/rocksdb/util/coding.h"
#include "yb/rocksdb/util/murmurhash.h"
#include "yb/rocksdb/util/mutexlock.h"
#include "yb/rocksdb/util/perf_context_imp.h"
#include "yb/rocksdb/util/statistics.h"
#include "yb/rocksdb/util/stop_watch.h"

#include "yb/util/mem_tracker.h"
#include "yb/util/stats/perf_step_timer.h"

using std::ostringstream;

namespace rocksdb {

MemTableOptions::MemTableOptions(
    const ImmutableCFOptions& ioptions,
    const MutableCFOptions& mutable_cf_options)
  : write_buffer_size(mutable_cf_options.write_buffer_size),
    arena_block_size(mutable_cf_options.arena_block_size),
    memtable_prefix_bloom_bits(mutable_cf_options.memtable_prefix_bloom_bits),
    memtable_prefix_bloom_probes(
        mutable_cf_options.memtable_prefix_bloom_probes),
    memtable_prefix_bloom_huge_page_tlb_size(
        mutable_cf_options.memtable_prefix_bloom_huge_page_tlb_size),
    inplace_update_support(ioptions.inplace_update_support),
    inplace_update_num_locks(mutable_cf_options.inplace_update_num_locks),
    inplace_callback(ioptions.inplace_callback),
    max_successive_merges(mutable_cf_options.max_successive_merges),
    filter_deletes(mutable_cf_options.filter_deletes),
    statistics(ioptions.statistics),
    merge_operator(ioptions.merge_operator),
    info_log(ioptions.info_log) {
  if (ioptions.mem_tracker) {
    mem_tracker = yb::MemTracker::FindOrCreateTracker("MemTable", ioptions.mem_tracker);
  }
}

MemTable::MemTable(const InternalKeyComparator& cmp,
                   const ImmutableCFOptions& ioptions,
                   const MutableCFOptions& mutable_cf_options,
                   WriteBuffer* write_buffer, SequenceNumber earliest_seq)
    : comparator_(cmp),
      moptions_(ioptions, mutable_cf_options),
      refs_(0),
      kArenaBlockSize(OptimizeBlockSize(moptions_.arena_block_size)),
      arena_(moptions_.arena_block_size, 0),
      allocator_(&arena_, write_buffer),
      table_(ioptions.memtable_factory->CreateMemTableRep(
          comparator_, &allocator_, ioptions.prefix_extractor,
          ioptions.info_log)),
      data_size_(0),
      num_entries_(0),
      num_deletes_(0),
      flush_in_progress_(false),
      flush_completed_(false),
      file_number_(0),
      first_seqno_(0),
      earliest_seqno_(earliest_seq),
      mem_next_logfile_number_(0),
      locks_(moptions_.inplace_update_support
                 ? moptions_.inplace_update_num_locks
                 : 0),
      prefix_extractor_(ioptions.prefix_extractor),
      flush_state_(FlushState::kNotRequested),
      env_(ioptions.env) {
  UpdateFlushState();
  // something went wrong if we need to flush before inserting anything
  assert(!ShouldScheduleFlush());

  if (prefix_extractor_ && moptions_.memtable_prefix_bloom_bits > 0) {
    prefix_bloom_.reset(new DynamicBloom(
        &allocator_,
        moptions_.memtable_prefix_bloom_bits, ioptions.bloom_locality,
        moptions_.memtable_prefix_bloom_probes, nullptr,
        moptions_.memtable_prefix_bloom_huge_page_tlb_size,
        ioptions.info_log));
  }

  if (moptions_.mem_tracker) {
    arena_.SetMemTracker(moptions_.mem_tracker);
  }
}

MemTable::~MemTable() { DCHECK_EQ(refs_, 0); }

size_t MemTable::ApproximateMemoryUsage() {
  size_t arena_usage = arena_.ApproximateMemoryUsage();
  size_t table_usage = table_->ApproximateMemoryUsage();
  // let MAX_USAGE =  std::numeric_limits<size_t>::max()
  // then if arena_usage + total_usage >= MAX_USAGE, return MAX_USAGE.
  // the following variation is to avoid numeric overflow.
  if (arena_usage >= std::numeric_limits<size_t>::max() - table_usage) {
    return std::numeric_limits<size_t>::max();
  }
  // otherwise, return the actual usage
  return arena_usage + table_usage;
}

bool MemTable::ShouldFlushNow() const {
  // In a lot of times, we cannot allocate arena blocks that exactly matches the
  // buffer size. Thus we have to decide if we should over-allocate or
  // under-allocate.
  // This constant variable can be interpreted as: if we still have more than
  // "kAllowOverAllocationRatio * kArenaBlockSize" space left, we'd try to over
  // allocate one more block.
  const double kAllowOverAllocationRatio = 0.6;

  // If arena still have room for new block allocation, we can safely say it
  // shouldn't flush.
  auto allocated_memory =
      table_->ApproximateMemoryUsage() + arena_.MemoryAllocatedBytes();

  // if we can still allocate one more block without exceeding the
  // over-allocation ratio, then we should not flush.
  if (allocated_memory + kArenaBlockSize <
      moptions_.write_buffer_size +
      kArenaBlockSize * kAllowOverAllocationRatio) {
    return false;
  }

  // if user keeps adding entries that exceeds moptions.write_buffer_size,
  // we need to flush earlier even though we still have much available
  // memory left.
  if (allocated_memory > moptions_.write_buffer_size +
      kArenaBlockSize * kAllowOverAllocationRatio) {
    return true;
  }

  // In this code path, Arena has already allocated its "last block", which
  // means the total allocatedmemory size is either:
  //  (1) "moderately" over allocated the memory (no more than `0.6 * arena
  // block size`. Or,
  //  (2) the allocated memory is less than write buffer size, but we'll stop
  // here since if we allocate a new arena block, we'll over allocate too much
  // more (half of the arena block size) memory.
  //
  // In either case, to avoid over-allocate, the last block will stop allocation
  // when its usage reaches a certain ratio, which we carefully choose "0.75
  // full" as the stop condition because it addresses the following issue with
  // great simplicity: What if the next inserted entry's size is
  // bigger than AllocatedAndUnused()?
  //
  // The answer is: if the entry size is also bigger than 0.25 *
  // kArenaBlockSize, a dedicated block will be allocated for it; otherwise
  // arena will anyway skip the AllocatedAndUnused() and allocate a new, empty
  // and regular block. In either case, we *overly* over-allocated.
  //
  // Therefore, setting the last block to be at most "0.75 full" avoids both
  // cases.
  //
  // NOTE: the average percentage of waste space of this approach can be counted
  // as: "arena block size * 0.25 / write buffer size". User who specify a small
  // write buffer size and/or big arena block size may suffer.
  return arena_.AllocatedAndUnused() < kArenaBlockSize / 4;
}

void MemTable::UpdateFlushState() {
  auto state = flush_state_.load(std::memory_order_relaxed);
  if (state == FlushState::kNotRequested && ShouldFlushNow()) {
    // ignore CAS failure, because that means somebody else requested
    // a flush
    flush_state_.compare_exchange_strong(state, FlushState::kRequested,
                                         std::memory_order_relaxed,
                                         std::memory_order_relaxed);
  }
}

int MemTable::KeyComparator::operator()(const char* prefix_len_key1,
                                        const char* prefix_len_key2) const {
  // Internal keys are encoded as length-prefixed strings.
  Slice k1 = GetLengthPrefixedSlice(prefix_len_key1);
  Slice k2 = GetLengthPrefixedSlice(prefix_len_key2);
  return comparator.Compare(k1, k2);
}

int MemTable::KeyComparator::operator()(const char* prefix_len_key,
                                        const Slice& key)
    const {
  // Internal keys are encoded as length-prefixed strings.
  Slice a = GetLengthPrefixedSlice(prefix_len_key);
  return comparator.Compare(a, key);
}

Slice MemTableRep::UserKey(const char* key) const {
  Slice slice = GetLengthPrefixedSlice(key);
  return Slice(slice.data(), slice.size() - 8);
}

KeyHandle MemTableRep::Allocate(const size_t len, char** buf) {
  *buf = allocator_->Allocate(len);
  return static_cast<KeyHandle>(*buf);
}

// Encode a suitable internal key target for "target" and return it.
// Uses *scratch as scratch space, and the returned pointer will point
// into this scratch space.
const char* EncodeKey(std::string* scratch, const Slice& target) {
  scratch->clear();
  PutVarint32(scratch, static_cast<uint32_t>(target.size()));
  scratch->append(target.cdata(), target.size());
  return scratch->data();
}

class MemTableIterator : public InternalIterator {
 public:
  MemTableIterator(
      const MemTable& mem, const ReadOptions& read_options, Arena* arena)
      : bloom_(nullptr),
        prefix_extractor_(mem.prefix_extractor_),
        arena_mode_(arena != nullptr) {
    if (prefix_extractor_ != nullptr && !read_options.total_order_seek) {
      bloom_ = mem.prefix_bloom_.get();
      iter_ = mem.table_->GetDynamicPrefixIterator(arena);
    } else {
      iter_ = mem.table_->GetIterator(arena);
    }
  }

  ~MemTableIterator() {
    if (arena_mode_) {
      iter_->~Iterator();
    } else {
      delete iter_;
    }
  }

  const KeyValueEntry& Seek(Slice k) override {
    PERF_TIMER_GUARD(seek_on_memtable_time);
    PERF_COUNTER_ADD(seek_on_memtable_count, 1);
    if (bloom_ != nullptr) {
      if (!bloom_->MayContain(
              prefix_extractor_->Transform(ExtractUserKey(k)))) {
        PERF_COUNTER_ADD(bloom_memtable_miss_count, 1);
        entry_ = KeyValueEntry::Invalid();
        return entry_;
      } else {
        PERF_COUNTER_ADD(bloom_memtable_hit_count, 1);
      }
    }
    return UpdateFetchResult(iter_->Seek(k));
  }

  const KeyValueEntry& SeekToFirst() override {
    return UpdateFetchResult(iter_->SeekToFirst());
  }

  const KeyValueEntry& SeekToLast() override {
    return UpdateFetchResult(iter_->SeekToLast());
  }

  const KeyValueEntry& Next() override {
    assert(Valid());
    return UpdateFetchResult(iter_->Next());
  }

  const KeyValueEntry& Prev() override {
    assert(Valid());
    return UpdateFetchResult(iter_->Prev());
  }

  const KeyValueEntry& UpdateFetchResult(const char* entry) {
    if (!entry) {
      entry_.Reset();
      return entry_;
    }

    Slice key_slice = GetLengthPrefixedSlice(entry);
    entry_.key = key_slice;
    entry_.value = GetLengthPrefixedSlice(key_slice.cend());
    return entry_;
  }

  const KeyValueEntry& Entry() const override {
    return entry_;
  }

  Status status() const override { return Status::OK(); }

  Status PinData() override {
    // memtable data is always pinned
    return Status::OK();
  }

  Status ReleasePinnedData() override {
    // memtable data is always pinned
    return Status::OK();
  }

  bool IsKeyPinned() const override {
    // memtable data is always pinned
    return true;
  }

  ScanForwardResult ScanForward(
      const Comparator* user_key_comparator, const Slice& upperbound,
      KeyFilterCallback* key_filter_callback, ScanCallback* scan_callback) override {
    LOG_IF(DFATAL, !Valid()) << "Iterator should be valid.";

    ScanForwardResult result;
    do {
      const auto user_key = ExtractUserKey(key());
      if (!upperbound.empty() && user_key_comparator->Compare(user_key, upperbound) >= 0) {
        break;
      }

      bool skip = false;
      if (key_filter_callback) {
        auto kf_result =
            (*key_filter_callback)(/*prefixed_key=*/ Slice(), /*shared_bytes=*/ 0, user_key);
        skip = kf_result.skip_key;
      }

      if (!skip && !(*scan_callback)(user_key, value())) {
        result.reached_upperbound = false;
        return result;
      }

      result.number_of_keys_visited++;
      Next();
    } while (Valid());

    result.reached_upperbound = true;
    return result;
  }

 private:
  DynamicBloom* bloom_;
  const SliceTransform* const prefix_extractor_;
  MemTableRep::Iterator* iter_;
  KeyValueEntry entry_;
  bool arena_mode_;

  // No copying allowed
  MemTableIterator(const MemTableIterator&);
  void operator=(const MemTableIterator&);
};

InternalIterator* MemTable::NewIterator(const ReadOptions& read_options,
                                        Arena* arena) {
  assert(arena != nullptr);
  auto mem = arena->AllocateAligned(sizeof(MemTableIterator));
  return new (mem) MemTableIterator(*this, read_options, arena);
}

port::RWMutex* MemTable::GetLock(const Slice& key) {
  static murmur_hash hash;
  return &locks_[hash(key) % locks_.size()];
}

std::string MemTable::ToString() const {
  ostringstream ss;
  auto* frontiers = Frontiers();
  ss << "MemTable {"
     << " num_entries: " << num_entries()
     << " num_deletes: " << num_deletes()
     << " IsEmpty: " << IsEmpty()
     << " flush_state: " << flush_state_
     << " first_seqno: " << GetFirstSequenceNumber()
     << " eariest_seqno: " << GetEarliestSequenceNumber()
     << " frontiers: ";
  if (frontiers) {
    ss << frontiers->ToString();
  } else {
    ss << "N/A";
  }
  ss << " }";
  return ss.str();
}

uint64_t MemTable::ApproximateSize(const Slice& start_ikey,
                                   const Slice& end_ikey) {
  uint64_t entry_count = table_->ApproximateNumEntries(start_ikey, end_ikey);
  if (entry_count == 0) {
    return 0;
  }
  uint64_t n = num_entries_.load(std::memory_order_relaxed);
  if (n == 0) {
    return 0;
  }
  if (entry_count > n) {
    // table_->ApproximateNumEntries() is just an estimate so it can be larger
    // than actual entries we have. Cap it to entries we have to limit the
    // inaccuracy.
    entry_count = n;
  }
  uint64_t data_size = data_size_.load(std::memory_order_relaxed);
  return entry_count * (data_size / n);
}

void MemTable::Add(SequenceNumber seq, ValueType type, const SliceParts& key,
                   const SliceParts& value, bool allow_concurrent) {
  PreparedAdd prepared_add;
  auto handle = PrepareAdd(seq, type, key, value, &prepared_add);
  ApplyPreparedAdd(&handle, 1, prepared_add, allow_concurrent);
}

KeyHandle MemTable::PrepareAdd(SequenceNumber s, ValueType type,
                               const SliceParts& key,
                               const SliceParts& value,
                               PreparedAdd* prepared_add) {
  // Format of an entry is concatenation of:
  //  key_size     : varint32 of internal_key.size()
  //  key bytes    : char[internal_key.size()]
  //  value_size   : varint32 of value.size()
  //  value bytes  : char[value.size()]
  uint32_t key_size = static_cast<uint32_t>(key.SumSizes());
  uint32_t val_size = static_cast<uint32_t>(value.SumSizes());
  uint32_t internal_key_size = key_size + 8;
  const uint32_t encoded_len = VarintLength(internal_key_size) +
                               internal_key_size + VarintLength(val_size) +
                               val_size;
  char* buf = nullptr;
  KeyHandle handle = table_->Allocate(encoded_len, &buf);

  char* p = EncodeVarint32(buf, internal_key_size);
  auto* begin = p;
  p = key.CopyAllTo(p);
  prepared_add->last_key = Slice(begin, p);
  uint64_t packed = PackSequenceAndType(s, type);
  EncodeFixed64(p, packed);
  p += 8;
  p = EncodeVarint32(p, val_size);
  begin = p;
  p = value.CopyAllTo(p);
  prepared_add->last_value = Slice(begin, p);
  assert((unsigned)(p - buf) == (unsigned)encoded_len);

  if (prefix_bloom_) {
    assert(prefix_extractor_);
    prefix_bloom_->Add(prefix_extractor_->Transform(key.TheOnlyPart()));
  }

  if (!prepared_add->min_seq_no) {
    prepared_add->min_seq_no = s;
  }
  prepared_add->total_encoded_len += encoded_len;
  if (type == ValueType::kTypeDeletion) {
    ++prepared_add->num_deletes;
  }
  return handle;
}

void MemTable::ApplyPreparedAdd(
    const KeyHandle* handle, size_t count, const PreparedAdd& prepared_add, bool allow_concurrent) {
  if (!allow_concurrent) {
    for (const auto* end = handle + count; handle != end; ++handle) {
      table_->Insert(*handle);
    }

    // this is a bit ugly, but is the way to avoid locked instructions
    // when incrementing an atomic
    num_entries_.store(num_entries_.load(std::memory_order_relaxed) + count,
                       std::memory_order_relaxed);
    data_size_.store(data_size_.load(std::memory_order_relaxed) + prepared_add.total_encoded_len,
                     std::memory_order_relaxed);
    if (prepared_add.num_deletes) {
      num_deletes_.store(num_deletes_.load(std::memory_order_relaxed) + prepared_add.num_deletes,
                         std::memory_order_relaxed);
    }

    // The first sequence number inserted into the memtable.
    // Multiple occurences of the same sequence number in the write batch are allowed
    // as long as they touch different keys.
    DCHECK(first_seqno_ == 0 || prepared_add.min_seq_no >= first_seqno_)
        << "first_seqno_: " << first_seqno_ << ", prepared_add.min_seq_no: "
        << prepared_add.min_seq_no;

    if (first_seqno_ == 0) {
      first_seqno_.store(prepared_add.min_seq_no, std::memory_order_relaxed);

      if (earliest_seqno_ == kMaxSequenceNumber) {
        earliest_seqno_.store(GetFirstSequenceNumber(),
                              std::memory_order_relaxed);
      }
      DCHECK_GE(first_seqno_.load(), earliest_seqno_.load());
    }
  } else {
    for (const auto* end = handle + count; handle != end; ++handle) {
      table_->InsertConcurrently(*handle);
    }

    num_entries_.fetch_add(count, std::memory_order_relaxed);
    data_size_.fetch_add(prepared_add.total_encoded_len, std::memory_order_relaxed);
    if (prepared_add.num_deletes) {
      num_deletes_.fetch_add(prepared_add.num_deletes, std::memory_order_relaxed);
    }

    // atomically update first_seqno_ and earliest_seqno_.
    uint64_t cur_seq_num = first_seqno_.load(std::memory_order_relaxed);
    while ((cur_seq_num == 0 || prepared_add.min_seq_no < cur_seq_num) &&
           !first_seqno_.compare_exchange_weak(cur_seq_num, prepared_add.min_seq_no)) {
    }
    uint64_t cur_earliest_seqno =
        earliest_seqno_.load(std::memory_order_relaxed);
    while (
        (cur_earliest_seqno == kMaxSequenceNumber ||
             prepared_add.min_seq_no < cur_earliest_seqno) &&
        !first_seqno_.compare_exchange_weak(cur_earliest_seqno, prepared_add.min_seq_no)) {
    }
  }

  UpdateFlushState();
}

// This comparator is used for deciding whether to erase a found key from a memtable instead of
// writing a deletion mark. This is exactly what we need for erasing records in memory
// (without writing new deletion marks). It expects a special key consisting of the user key being
// erased followed by 8 0xff bytes as the first argument, and a key from a memtable as the second
// argument (with the usual user_key + value_type + seqno format).
// It returns zero if the user key parts of both arguments match and the second argument's value
// type is not a deletion.
//
// Note: this comparator's return value cannot be used to establish order,
// only to test for "equality" as defined above.
class EraseHelperKeyComparator : public MemTableRep::KeyComparator {
 public:
  explicit EraseHelperKeyComparator(const Comparator* user_comparator, bool* had_delete)
      : user_comparator_(user_comparator), had_delete_(had_delete) {}

  int operator()(const char* prefix_len_key1, const char* prefix_len_key2) const override {
    // Internal keys are encoded as length-prefixed strings.
    Slice k1 = GetLengthPrefixedSlice(prefix_len_key1);
    Slice k2 = GetLengthPrefixedSlice(prefix_len_key2);
    return Compare(k1, k2);
  }

  int operator()(const char* prefix_len_key, const Slice& key) const override {
    // Internal keys are encoded as length-prefixed strings.
    Slice a = GetLengthPrefixedSlice(prefix_len_key);
    return Compare(a, key);
  }

  int Compare(const Slice& a, const Slice& b) const {
    auto user_b = ExtractUserKey(b);
    auto result = user_comparator_->Compare(ExtractUserKey(a), user_b);
    if (result == 0) {
      // This comparator is used only to check whether we should delete the entry we found.
      // So any non zero result should satisfy our needs.
      // `b` is a value stored in mem table, so we check only it.
      // `a` is key that we created for erase and user key is always followed by eight 0xff.
      auto value_type = static_cast<ValueType>(b[user_b.size()]);
      DCHECK_LE(value_type, ValueType::kTypeColumnFamilySingleDeletion);
      if (value_type == ValueType::kTypeSingleDeletion ||
          value_type == ValueType::kTypeColumnFamilySingleDeletion) {
        *had_delete_ = true;
        return -1;
      }
    }
    return result;
  }

 private:
  const Comparator* user_comparator_;
  bool* had_delete_;
};

bool MemTable::Erase(const Slice& user_key) {
  uint32_t user_key_size = static_cast<uint32_t>(user_key.size());
  uint32_t internal_key_size = user_key_size + 8;
  const uint32_t encoded_len = VarintLength(internal_key_size) + internal_key_size;

  if (erase_key_buffer_.size() < encoded_len) {
    erase_key_buffer_.resize(encoded_len);
  }
  char* buf = erase_key_buffer_.data();
  char* p = EncodeVarint32(buf, internal_key_size);
  memcpy(p, user_key.data(), user_key_size);
  p += user_key_size;
  // Fill key tail with 0xffffffffffffffff so it we be less than actual user key.
  // Please note descending order is used for key tail.
  EncodeFixed64(p, -1LL);
  bool had_delete = false;
  EraseHelperKeyComparator only_user_key_comparator(
      comparator_.comparator.user_comparator(), &had_delete);
  if (table_->Erase(buf, only_user_key_comparator)) {
    // this is a bit ugly, but is the way to avoid locked instructions
    // when incrementing an atomic
    num_erased_.store(num_erased_.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);

    UpdateFlushState();
    return true;
  } else if (had_delete) { // Do nothing in case when we already had delete.
    return true;
  }

  return false;
}

// Callback from MemTable::Get()
namespace {

struct Saver {
  Status* status;
  const LookupKey* key;
  bool* found_final_value;  // Is value set correctly? Used by KeyMayExist
  bool* merge_in_progress;
  std::string* value;
  SequenceNumber seq;
  const MergeOperator* merge_operator;
  // the merge operations encountered;
  MergeContext* merge_context;
  MemTable* mem;
  Logger* logger;
  Statistics* statistics;
  bool inplace_update_support;
  Env* env_;
};
}  // namespace

static bool SaveValue(void* arg, const char* entry) {
  Saver* s = reinterpret_cast<Saver*>(arg);
  MergeContext* merge_context = s->merge_context;
  const MergeOperator* merge_operator = s->merge_operator;

  assert(s != nullptr && merge_context != nullptr);

  // entry format is:
  //    klength  varint32
  //    userkey  char[klength-8]
  //    tag      uint64
  //    vlength  varint32
  //    value    char[vlength]
  // Check that it belongs to same user key.  We do not check the
  // sequence number since the Seek() call above should have skipped
  // all entries with overly large sequence numbers.
  uint32_t key_length;
  const char* key_ptr = GetVarint32Ptr(entry, entry + 5, &key_length);
  if (s->mem->GetInternalKeyComparator().user_comparator()->Equal(
          Slice(key_ptr, key_length - 8), s->key->user_key())) {
    // Correct user key
    auto seq_and_type = UnPackSequenceAndTypeFromEnd(key_ptr + key_length);
    s->seq = seq_and_type.sequence;

    switch (seq_and_type.type) {
      case kTypeValue: {
        if (s->inplace_update_support) {
          s->mem->GetLock(s->key->user_key())->ReadLock();
        }
        Slice v = GetLengthPrefixedSlice(key_ptr + key_length);
        *(s->status) = Status::OK();
        if (*(s->merge_in_progress)) {
          assert(merge_operator);
          bool merge_success = false;
          {
            StopWatchNano timer(s->env_, s->statistics != nullptr);
            PERF_TIMER_GUARD(merge_operator_time_nanos);
            merge_success = merge_operator->FullMerge(
                s->key->user_key(), &v, merge_context->GetOperands(), s->value,
                s->logger);
            RecordTick(s->statistics, MERGE_OPERATION_TOTAL_TIME,
                       timer.ElapsedNanos());
          }
          if (!merge_success) {
            RecordTick(s->statistics, NUMBER_MERGE_FAILURES);
            *(s->status) =
                STATUS(Corruption, "Error: Could not perform merge.");
          }
        } else if (s->value != nullptr) {
          s->value->assign(v.cdata(), v.size());
        }
        if (s->inplace_update_support) {
          s->mem->GetLock(s->key->user_key())->ReadUnlock();
        }
        *(s->found_final_value) = true;
        return false;
      }
      case kTypeDeletion:
      case kTypeSingleDeletion: {
        if (*(s->merge_in_progress)) {
          assert(merge_operator != nullptr);
          *(s->status) = Status::OK();
          bool merge_success = false;
          {
            StopWatchNano timer(s->env_, s->statistics != nullptr);
            PERF_TIMER_GUARD(merge_operator_time_nanos);
            merge_success = merge_operator->FullMerge(
                s->key->user_key(), nullptr, merge_context->GetOperands(),
                s->value, s->logger);
            RecordTick(s->statistics, MERGE_OPERATION_TOTAL_TIME,
                       timer.ElapsedNanos());
          }
          if (!merge_success) {
            RecordTick(s->statistics, NUMBER_MERGE_FAILURES);
            *(s->status) =
                STATUS(Corruption, "Error: Could not perform merge.");
          }
        } else {
          *(s->status) = STATUS(NotFound, "");
        }
        *(s->found_final_value) = true;
        return false;
      }
      case kTypeMerge: {
        if (!merge_operator) {
          *(s->status) = STATUS(InvalidArgument,
              "merge_operator is not properly initialized.");
          // Normally we continue the loop (return true) when we see a merge
          // operand.  But in case of an error, we should stop the loop
          // immediately and pretend we have found the value to stop further
          // seek.  Otherwise, the later call will override this error status.
          *(s->found_final_value) = true;
          return false;
        }
        Slice v = GetLengthPrefixedSlice(key_ptr + key_length);
        *(s->merge_in_progress) = true;
        merge_context->PushOperand(v);
        return true;
      }
      default:
        assert(false);
        return true;
    }
  }

  // s->state could be Corrupt, merge or notfound
  return false;
}

bool MemTable::Get(const LookupKey& key, std::string* value, Status* s,
                   MergeContext* merge_context, SequenceNumber* seq) {
  // The sequence number is updated synchronously in version_set.h
  if (IsEmpty()) {
    // Avoiding recording stats for speed.
    return false;
  }
  PERF_TIMER_GUARD(get_from_memtable_time);

  Slice user_key = key.user_key();
  bool found_final_value = false;
  bool merge_in_progress = s->IsMergeInProgress();
  bool const may_contain =
      nullptr == prefix_bloom_
          ? false
          : prefix_bloom_->MayContain(prefix_extractor_->Transform(user_key));
  if (prefix_bloom_ && !may_contain) {
    // iter is null if prefix bloom says the key does not exist
    PERF_COUNTER_ADD(bloom_memtable_miss_count, 1);
    *seq = kMaxSequenceNumber;
  } else {
    if (prefix_bloom_) {
      PERF_COUNTER_ADD(bloom_memtable_hit_count, 1);
    }
    Saver saver;
    saver.status = s;
    saver.found_final_value = &found_final_value;
    saver.merge_in_progress = &merge_in_progress;
    saver.key = &key;
    saver.value = value;
    saver.seq = kMaxSequenceNumber;
    saver.mem = this;
    saver.merge_context = merge_context;
    saver.merge_operator = moptions_.merge_operator;
    saver.logger = moptions_.info_log;
    saver.inplace_update_support = moptions_.inplace_update_support;
    saver.statistics = moptions_.statistics;
    saver.env_ = env_;
    table_->Get(key, &saver, SaveValue);

    *seq = saver.seq;
  }

  // No change to value, since we have not yet found a Put/Delete
  if (!found_final_value && merge_in_progress) {
    *s = STATUS(MergeInProgress, "");
  }
  PERF_COUNTER_ADD(get_from_memtable_count, 1);
  return found_final_value;
}

void MemTable::Update(SequenceNumber seq,
                      const Slice& key,
                      const Slice& value) {
  LookupKey lkey(key, seq);
  Slice mem_key = lkey.memtable_key();

  std::unique_ptr<MemTableRep::Iterator> iter(table_->GetDynamicPrefixIterator());
  iter->SeekMemTableKey(lkey.internal_key(), mem_key.cdata());

  const char* entry = iter->Entry();
  if (entry) {
    // entry format is:
    //    key_length  varint32
    //    userkey  char[klength-8]
    //    tag      uint64
    //    vlength  varint32
    //    value    char[vlength]
    // Check that it belongs to same user key.  We do not check the
    // sequence number since the Seek() call above should have skipped
    // all entries with overly large sequence numbers.
    uint32_t key_length = 0;
    const char* key_ptr = GetVarint32Ptr(entry, entry + 5, &key_length);
    if (comparator_.comparator.user_comparator()->Equal(
            Slice(key_ptr, key_length - 8), lkey.user_key())) {
      // Correct user key
      auto seq_and_type = UnPackSequenceAndTypeFromEnd(key_ptr + key_length);
      switch (seq_and_type.type) {
        case kTypeValue: {
          Slice prev_value = GetLengthPrefixedSlice(key_ptr + key_length);
          uint32_t prev_size = static_cast<uint32_t>(prev_value.size());
          uint32_t new_size = static_cast<uint32_t>(value.size());

          // Update value, if new value size  <= previous value size
          if (new_size <= prev_size ) {
            char* p = EncodeVarint32(const_cast<char*>(key_ptr) + key_length,
                                     new_size);
            WriteLock wl(GetLock(lkey.user_key()));
            memcpy(p, value.data(), value.size());
            assert((unsigned)((p + value.size()) - entry) ==
                   (unsigned)(VarintLength(key_length) + key_length +
                              VarintLength(value.size()) + value.size()));
            return;
          }
          // TODO (YugaByte): verify this is not a bug. The behavior for kTypeValue in case there
          // is not enough room for an in-place update, .
          FALLTHROUGH_INTENDED;
        }
        default:
          // If the latest value is kTypeDeletion, kTypeMerge or kTypeLogData
          // we don't have enough space for update inplace
            Add(seq, kTypeValue, SliceParts(&key, 1), SliceParts(&value, 1));
            return;
      }
    }
  }

  // key doesn't exist
  Add(seq, kTypeValue, SliceParts(&key, 1), SliceParts(&value, 1));
}

bool MemTable::UpdateCallback(SequenceNumber seq,
                              const Slice& key,
                              const Slice& delta) {
  LookupKey lkey(key, seq);
  Slice memkey = lkey.memtable_key();

  std::unique_ptr<MemTableRep::Iterator> iter(table_->GetDynamicPrefixIterator());
  iter->SeekMemTableKey(lkey.internal_key(), memkey.cdata());

  const char* entry = iter->Entry();
  if (entry) {
    // entry format is:
    //    key_length  varint32
    //    userkey  char[klength-8]
    //    tag      uint64
    //    vlength  varint32
    //    value    char[vlength]
    // Check that it belongs to same user key.  We do not check the
    // sequence number since the Seek() call above should have skipped
    // all entries with overly large sequence numbers.
    uint32_t key_length = 0;
    const char* key_ptr = GetVarint32Ptr(entry, entry + 5, &key_length);
    if (comparator_.comparator.user_comparator()->Equal(
            Slice(key_ptr, key_length - 8), lkey.user_key())) {
      // Correct user key
      switch (UnPackSequenceAndTypeFromEnd(key_ptr + key_length).type) {
        case kTypeValue: {
          Slice prev_value = GetLengthPrefixedSlice(key_ptr + key_length);
          uint32_t prev_size = static_cast<uint32_t>(prev_value.size());

          char* prev_buffer = const_cast<char*>(prev_value.cdata());
          uint32_t new_prev_size = prev_size;

          std::string str_value;
          WriteLock wl(GetLock(lkey.user_key()));
          auto status = moptions_.inplace_callback(prev_buffer, &new_prev_size,
                                                   delta, &str_value);
          if (status == UpdateStatus::UPDATED_INPLACE) {
            // Value already updated by callback.
            assert(new_prev_size <= prev_size);
            if (new_prev_size < prev_size) {
              // overwrite the new prev_size
              char* p = EncodeVarint32(const_cast<char*>(key_ptr) + key_length,
                                       new_prev_size);
              if (VarintLength(new_prev_size) < VarintLength(prev_size)) {
                // shift the value buffer as well.
                memcpy(p, prev_buffer, new_prev_size);
              }
            }
            RecordTick(moptions_.statistics, NUMBER_KEYS_UPDATED);
            UpdateFlushState();
            return true;
          } else if (status == UpdateStatus::UPDATED) {
            Slice value(str_value);
            Add(seq, kTypeValue, SliceParts(&key, 1), SliceParts(&value, 1));
            RecordTick(moptions_.statistics, NUMBER_KEYS_WRITTEN);
            UpdateFlushState();
            return true;
          } else if (status == UpdateStatus::UPDATE_FAILED) {
            // No action required. Return.
            UpdateFlushState();
            return true;
          }
          FALLTHROUGH_INTENDED;
        }
        default:
          break;
      }
    }
  }
  // If the latest value is not kTypeValue
  // or key doesn't exist
  return false;
}

size_t MemTable::CountSuccessiveMergeEntries(const LookupKey& key) {
  Slice memkey = key.memtable_key();

  // A total ordered iterator is costly for some memtablerep (prefix aware
  // reps). By passing in the user key, we allow efficient iterator creation.
  // The iterator only needs to be ordered within the same user key.
  std::unique_ptr<MemTableRep::Iterator> iter(
      table_->GetDynamicPrefixIterator());
  iter->SeekMemTableKey(key.internal_key(), memkey.cdata());

  size_t num_successive_merges = 0;

  for (; const char* entry = iter->Entry(); iter->Next()) {
    uint32_t key_length = 0;
    const char* iter_key_ptr = GetVarint32Ptr(entry, entry + 5, &key_length);
    if (!comparator_.comparator.user_comparator()->Equal(
            Slice(iter_key_ptr, key_length - 8), key.user_key())) {
      break;
    }

    if (UnPackSequenceAndTypeFromEnd(iter_key_ptr + key_length).type != kTypeMerge) {
      break;
    }

    ++num_successive_merges;
  }

  return num_successive_merges;
}

UserFrontierPtr MemTable::GetFrontier(UpdateUserValueType type) const {
  std::lock_guard l(frontiers_mutex_);
  if (!frontiers_) {
    return nullptr;
  }

  switch (type) {
    case UpdateUserValueType::kSmallest:
      return frontiers_->Smallest().Clone();
    case UpdateUserValueType::kLargest:
      return frontiers_->Largest().Clone();
  }

  FATAL_INVALID_ENUM_VALUE(UpdateUserValueType, type);
}

void MemTableRep::Get(const LookupKey& k, void* callback_args,
                      bool (*callback_func)(void* arg, const char* entry)) {
  auto iter = GetDynamicPrefixIterator();
  for (iter->SeekMemTableKey(k.internal_key(), k.memtable_key().cdata());; iter->Next()) {
    auto entry = iter->Entry();
    if (!entry || !callback_func(callback_args, entry)) {
      break;
    }
  }
}

}  // namespace rocksdb
