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
//
// WriteBatch::rep_ :=
//    sequence: fixed64
//    count: fixed32
//    data: record[count]
// record :=
//    kTypeValue varstring varstring
//    kTypeDeletion varstring
//    kTypeSingleDeletion varstring
//    kTypeMerge varstring varstring
//    kTypeColumnFamilyValue varint32 varstring varstring
//    kTypeColumnFamilyDeletion varint32 varstring varstring
//    kTypeColumnFamilySingleDeletion varint32 varstring varstring
//    kTypeColumnFamilyMerge varint32 varstring varstring
// varstring :=
//    len: varint32
//    data: uint8[len]
//
// YugaByte-specific extensions stored out-of-band:
//   user_sequence_numbers_

#include <stack>
#include <stdexcept>
#include <vector>

#include "yb/rocksdb/db/column_family.h"
#include "yb/rocksdb/db/db_impl.h"
#include "yb/rocksdb/db/dbformat.h"
#include "yb/rocksdb/db/flush_scheduler.h"
#include "yb/rocksdb/db/memtable.h"
#include "yb/rocksdb/db/snapshot_impl.h"
#include "yb/rocksdb/db/write_batch_internal.h"
#include "yb/rocksdb/merge_operator.h"
#include "yb/rocksdb/util/coding.h"
#include "yb/rocksdb/util/perf_context_imp.h"
#include "yb/rocksdb/util/statistics.h"

#include "yb/util/stats/perf_step_timer.h"
#include "yb/util/faststring.h"

namespace rocksdb {

// anon namespace for file-local types
namespace {

enum ContentFlags : uint32_t {
  DEFERRED = 1,
  HAS_PUT = 2,
  HAS_DELETE = 4,
  HAS_SINGLE_DELETE = 8,
  HAS_MERGE = 16,
  HAS_FRONTIERS = 32,
};

struct BatchContentClassifier : public WriteBatch::Handler {
  uint32_t content_flags = 0;

  Status PutCF(uint32_t, const SliceParts&, const SliceParts&) override {
    content_flags |= ContentFlags::HAS_PUT;
    return Status::OK();
  }

  Status DeleteCF(uint32_t, const Slice&) override {
    content_flags |= ContentFlags::HAS_DELETE;
    return Status::OK();
  }

  Status SingleDeleteCF(uint32_t, const Slice&) override {
    content_flags |= ContentFlags::HAS_SINGLE_DELETE;
    return Status::OK();
  }

  Status MergeCF(uint32_t, const Slice&, const Slice&) override {
    content_flags |= ContentFlags::HAS_MERGE;
    return Status::OK();
  }

  Status Frontiers(const UserFrontiers& range) override {
    content_flags |= ContentFlags::HAS_FRONTIERS;
    return Status::OK();
  }
};

class DirectWriteHandlerImpl : public DirectWriteHandler {
 public:
  explicit DirectWriteHandlerImpl(
      MemTable* mem_table, SequenceNumber seq, WriteBatch::Handler* handler_for_logging)
      : mem_table_(mem_table), seq_(seq), handler_for_logging_(handler_for_logging) {}

  std::pair<Slice, Slice> Put(const SliceParts& key, const SliceParts& value) override {
    if (handler_for_logging_) {
      WARN_NOT_OK(handler_for_logging_->PutCF(0 /* column_family_id */, key, value),
                  "Logging handler failed on PutCF");
    }
    Add(ValueType::kTypeValue, key, value);
    return std::pair(prepared_add_.last_key, prepared_add_.last_value);
  }

  void SingleDelete(const Slice& key) override {
    if (handler_for_logging_) {
      WARN_NOT_OK(handler_for_logging_->SingleDeleteCF(0 /* column_family_id */, key),
                  "Logging handler failed on SingleDeleteCF");
    }
    if (mem_table_->Erase(key)) {
      return;
    }
    Add(ValueType::kTypeSingleDeletion, SliceParts(&key, 1), SliceParts());
  }

  size_t Complete() {
    if (keys_.empty()) {
      return 0;
    }
    auto compare =
        [comparator = &mem_table_->GetInternalKeyComparator()](KeyHandle lhs, KeyHandle rhs) {
      auto lhs_slice = GetLengthPrefixedSlice(static_cast<const char*>(lhs));
      auto rhs_slice = GetLengthPrefixedSlice(static_cast<const char*>(rhs));
      return comparator->Compare(lhs_slice, rhs_slice) < 0;
    };
    std::sort(keys_.begin(), keys_.end(), compare);
    mem_table_->ApplyPreparedAdd(keys_.data(), keys_.size(), prepared_add_, false);
    return keys_.size();
  }

 private:
  void Add(ValueType value_type, const SliceParts& key, const SliceParts& value) {
    keys_.push_back(
        mem_table_->PrepareAdd(seq_++, value_type, key, value, &prepared_add_));
  }

  MemTable* mem_table_;
  SequenceNumber seq_;
  WriteBatch::Handler* handler_for_logging_;
  PreparedAdd prepared_add_;
  boost::container::small_vector<KeyHandle, 128> keys_;
};

}  // anon namespace

// WriteBatch header has an 8-byte sequence number followed by a 4-byte count.
static const size_t kHeader = 12;

struct SavePoint {
  size_t size;  // size of rep_
  uint32_t count;    // count of elements in rep_
  uint32_t content_flags;
  const UserFrontiers* frontiers;
};

struct SavePoints {
  std::stack<SavePoint> stack;
};

WriteBatch::WriteBatch(size_t reserved_bytes)
    : content_flags_(0) {
  rep_.reserve(std::max(reserved_bytes, kHeader));
  rep_.resize(kHeader);
}

WriteBatch::WriteBatch(const std::string& rep)
    : content_flags_(ContentFlags::DEFERRED),
      rep_(rep) {}

WriteBatch::WriteBatch(const WriteBatch& src)
    : content_flags_(src.content_flags_.load(std::memory_order_relaxed)),
      rep_(src.rep_),
      frontiers_(src.frontiers_) {
  if (src.save_points_) {
    save_points_.reset(new SavePoints(*src.save_points_));
  }
}

WriteBatch::WriteBatch(WriteBatch&& src)
    : save_points_(std::move(src.save_points_)),
      content_flags_(src.content_flags_.load(std::memory_order_relaxed)),
      rep_(std::move(src.rep_)),
      frontiers_(std::move(src.frontiers_)) {}

WriteBatch& WriteBatch::operator=(const WriteBatch& src) {
  if (&src != this) {
    this->~WriteBatch();
    new (this) WriteBatch(src);
  }
  return *this;
}

WriteBatch& WriteBatch::operator=(WriteBatch&& src) {
  if (&src != this) {
    this->~WriteBatch();
    new (this) WriteBatch(std::move(src));
  }
  return *this;
}

WriteBatch::~WriteBatch() {}

WriteBatch::Handler::~Handler() { }

void WriteBatch::Handler::LogData(const Slice& blob) {
  // If the user has not specified something to do with blobs, then we ignore
  // them.
}

bool WriteBatch::Handler::Continue() {
  return true;
}

void WriteBatch::Clear() {
  rep_.clear();
  rep_.resize(kHeader);

  content_flags_.store(0, std::memory_order_relaxed);

  if (save_points_ != nullptr) {
    while (!save_points_->stack.empty()) {
      save_points_->stack.pop();
    }
  }

  frontiers_ = nullptr;
}

uint32_t WriteBatch::Count() const {
  return WriteBatchInternal::Count(this);
}

uint32_t WriteBatch::ComputeContentFlags() const {
  auto rv = content_flags_.load(std::memory_order_relaxed);
  if ((rv & ContentFlags::DEFERRED) != 0) {
    BatchContentClassifier classifier;
    auto status = Iterate(&classifier);
    LOG_IF(ERROR, !status.ok()) << "Iterate failed during ComputeContentFlags: " << status;
    rv = classifier.content_flags;

    // this method is conceptually const, because it is performing a lazy
    // computation that doesn't affect the abstract state of the batch.
    // content_flags_ is marked mutable so that we can perform the
    // following assignment
    content_flags_.store(rv, std::memory_order_relaxed);
  }
  return rv;
}

bool WriteBatch::HasPut() const {
  return (ComputeContentFlags() & ContentFlags::HAS_PUT) != 0;
}

bool WriteBatch::HasDelete() const {
  return (ComputeContentFlags() & ContentFlags::HAS_DELETE) != 0;
}

bool WriteBatch::HasSingleDelete() const {
  return (ComputeContentFlags() & ContentFlags::HAS_SINGLE_DELETE) != 0;
}

bool WriteBatch::HasMerge() const {
  return (ComputeContentFlags() & ContentFlags::HAS_MERGE) != 0;
}

Status ReadRecordFromWriteBatch(Slice* input, char* tag,
                                uint32_t* column_family, Slice* key,
                                Slice* value, Slice* blob) {
  assert(key != nullptr && value != nullptr);
  *tag = (*input)[0];
  input->remove_prefix(1);
  *column_family = 0;  // default
  switch (*tag) {
    case kTypeColumnFamilyValue:
      if (!GetVarint32(input, column_family)) {
        return STATUS(Corruption, "bad WriteBatch Put");
      }
      FALLTHROUGH_INTENDED;
    case kTypeValue:
      if (!GetLengthPrefixedSlice(input, key) ||
          !GetLengthPrefixedSlice(input, value)) {
        return STATUS(Corruption, "bad WriteBatch Put");
      }
      break;
    case kTypeColumnFamilyDeletion:
    case kTypeColumnFamilySingleDeletion:
      if (!GetVarint32(input, column_family)) {
        return STATUS(Corruption, "bad WriteBatch Delete");
      }
      FALLTHROUGH_INTENDED;
    case kTypeDeletion:
    case kTypeSingleDeletion:
      if (!GetLengthPrefixedSlice(input, key)) {
        return STATUS(Corruption, "bad WriteBatch Delete");
      }
      break;
    case kTypeColumnFamilyMerge:
      if (!GetVarint32(input, column_family)) {
        return STATUS(Corruption, "bad WriteBatch Merge");
      }
      FALLTHROUGH_INTENDED;
    case kTypeMerge:
      if (!GetLengthPrefixedSlice(input, key) ||
          !GetLengthPrefixedSlice(input, value)) {
        return STATUS(Corruption, "bad WriteBatch Merge");
      }
      break;
    case kTypeLogData:
      assert(blob != nullptr);
      if (!GetLengthPrefixedSlice(input, blob)) {
        return STATUS(Corruption, "bad WriteBatch Blob");
      }
      break;
    default:
      return STATUS(Corruption, "unknown WriteBatch tag");
  }
  return Status::OK();
}

Result<size_t> DirectInsert(
    WriteBatch::Handler* handler, DirectWriter* writer, WriteBatch::Handler* handler_for_logging);

Status WriteBatch::Iterate(Handler* handler) const {
  Slice input(rep_);
  if (input.size() < kHeader) {
    return STATUS(Corruption, "malformed WriteBatch (too small)");
  }

  input.remove_prefix(kHeader);
  Slice key, value, blob;
  size_t found = 0;
  Status s;

  if (s.ok() && direct_writer_) {
    auto result = DirectInsert(handler, direct_writer_, handler_for_logging_);
    if (result.ok()) {
      direct_entries_ = *result;
    } else {
      s = result.status();
    }
  }
  if (frontiers_) {
    s = handler->Frontiers(*frontiers_);
  }
  while (s.ok() && !input.empty() && handler->Continue()) {
    char tag = 0;
    uint32_t column_family = 0;  // default

    s = ReadRecordFromWriteBatch(&input, &tag, &column_family, &key, &value,
                                 &blob);
    if (!s.ok()) {
      return s;
    }

    switch (tag) {
      case kTypeColumnFamilyValue:
      case kTypeValue:
        assert(content_flags_.load(std::memory_order_relaxed) &
               (ContentFlags::DEFERRED | ContentFlags::HAS_PUT));
        s = handler->PutCF(column_family, SliceParts(&key, 1), SliceParts(&value, 1));
        found++;
        break;
      case kTypeColumnFamilyDeletion:
      case kTypeDeletion:
        assert(content_flags_.load(std::memory_order_relaxed) &
               (ContentFlags::DEFERRED | ContentFlags::HAS_DELETE));
        s = handler->DeleteCF(column_family, key);
        found++;
        break;
      case kTypeColumnFamilySingleDeletion:
      case kTypeSingleDeletion:
        assert(content_flags_.load(std::memory_order_relaxed) &
               (ContentFlags::DEFERRED | ContentFlags::HAS_SINGLE_DELETE));
        s = handler->SingleDeleteCF(column_family, key);
        found++;
        break;
      case kTypeColumnFamilyMerge:
      case kTypeMerge:
        assert(content_flags_.load(std::memory_order_relaxed) &
               (ContentFlags::DEFERRED | ContentFlags::HAS_MERGE));
        s = handler->MergeCF(column_family, key, value);
        found++;
        break;
      case kTypeLogData:
        handler->LogData(blob);
        break;
      default:
        return STATUS(Corruption, "unknown WriteBatch tag");
    }
  }
  if (!s.ok()) {
    return s;
  }
  if (found != WriteBatchInternal::Count(this)) {
    return STATUS(Corruption, "WriteBatch has wrong count");
  } else {
    return Status::OK();
  }
}

uint32_t WriteBatchInternal::Count(const WriteBatch* b) {
  return DecodeFixed32(b->rep_.data() + 8);
}

void WriteBatchInternal::SetCount(WriteBatch* b, uint32_t n) {
  EncodeFixed32(&b->rep_[8], n);
}

SequenceNumber WriteBatchInternal::Sequence(const WriteBatch* b) {
  return SequenceNumber(DecodeFixed64(b->rep_.data()));
}

void WriteBatchInternal::SetSequence(WriteBatch* b, SequenceNumber seq) {
  EncodeFixed64(&b->rep_[0], seq);
}

size_t WriteBatchInternal::GetFirstOffset(WriteBatch* b) { return kHeader; }

void WriteBatchInternal::Put(WriteBatch* b, uint32_t column_family_id,
                             const Slice& key, const Slice& value) {
  WriteBatchInternal::SetCount(b, WriteBatchInternal::Count(b) + 1);
  if (column_family_id == 0) {
    b->rep_.push_back(static_cast<char>(kTypeValue));
  } else {
    b->rep_.push_back(static_cast<char>(kTypeColumnFamilyValue));
    PutVarint32(&b->rep_, column_family_id);
  }
  PutLengthPrefixedSlice(&b->rep_, key);
  PutLengthPrefixedSlice(&b->rep_, value);
  b->content_flags_.store(
      b->content_flags_.load(std::memory_order_relaxed) | ContentFlags::HAS_PUT,
      std::memory_order_relaxed);
}

void WriteBatch::Put(ColumnFamilyHandle* column_family, const Slice& key,
                     const Slice& value) {
  WriteBatchInternal::Put(this, GetColumnFamilyID(column_family), key, value);
}

void WriteBatchInternal::Put(WriteBatch* b, uint32_t column_family_id,
                             const SliceParts& key, const SliceParts& value) {
  WriteBatchInternal::SetCount(b, WriteBatchInternal::Count(b) + 1);
  if (column_family_id == 0) {
    b->rep_.push_back(static_cast<char>(kTypeValue));
  } else {
    b->rep_.push_back(static_cast<char>(kTypeColumnFamilyValue));
    PutVarint32(&b->rep_, column_family_id);
  }
  PutLengthPrefixedSliceParts(&b->rep_, key);
  PutLengthPrefixedSliceParts(&b->rep_, value);
  b->content_flags_.store(
      b->content_flags_.load(std::memory_order_relaxed) | ContentFlags::HAS_PUT,
      std::memory_order_relaxed);
}

void WriteBatch::Put(ColumnFamilyHandle* column_family, const SliceParts& key,
                     const SliceParts& value) {
  WriteBatchInternal::Put(this, GetColumnFamilyID(column_family), key, value);
}

void WriteBatchInternal::Delete(WriteBatch* b, uint32_t column_family_id,
                                const Slice& key) {
  WriteBatchInternal::SetCount(b, WriteBatchInternal::Count(b) + 1);
  if (column_family_id == 0) {
    b->rep_.push_back(static_cast<char>(kTypeDeletion));
  } else {
    b->rep_.push_back(static_cast<char>(kTypeColumnFamilyDeletion));
    PutVarint32(&b->rep_, column_family_id);
  }
  PutLengthPrefixedSlice(&b->rep_, key);
  b->content_flags_.store(b->content_flags_.load(std::memory_order_relaxed) |
                              ContentFlags::HAS_DELETE,
                          std::memory_order_relaxed);
}

void WriteBatch::Delete(ColumnFamilyHandle* column_family, const Slice& key) {
  WriteBatchInternal::Delete(this, GetColumnFamilyID(column_family), key);
}

void WriteBatchInternal::Delete(WriteBatch* b, uint32_t column_family_id,
                                const SliceParts& key) {
  WriteBatchInternal::SetCount(b, WriteBatchInternal::Count(b) + 1);
  if (column_family_id == 0) {
    b->rep_.push_back(static_cast<char>(kTypeDeletion));
  } else {
    b->rep_.push_back(static_cast<char>(kTypeColumnFamilyDeletion));
    PutVarint32(&b->rep_, column_family_id);
  }
  PutLengthPrefixedSliceParts(&b->rep_, key);
  b->content_flags_.store(b->content_flags_.load(std::memory_order_relaxed) |
                              ContentFlags::HAS_DELETE,
                          std::memory_order_relaxed);
}

void WriteBatch::Delete(ColumnFamilyHandle* column_family,
                        const SliceParts& key) {
  WriteBatchInternal::Delete(this, GetColumnFamilyID(column_family), key);
}

void WriteBatchInternal::SingleDelete(WriteBatch* b, uint32_t column_family_id,
                                      const Slice& key) {
  WriteBatchInternal::SetCount(b, WriteBatchInternal::Count(b) + 1);
  if (column_family_id == 0) {
    b->rep_.push_back(static_cast<char>(kTypeSingleDeletion));
  } else {
    b->rep_.push_back(static_cast<char>(kTypeColumnFamilySingleDeletion));
    PutVarint32(&b->rep_, column_family_id);
  }
  PutLengthPrefixedSlice(&b->rep_, key);
  b->content_flags_.store(b->content_flags_.load(std::memory_order_relaxed) |
                              ContentFlags::HAS_SINGLE_DELETE,
                          std::memory_order_relaxed);
}

void WriteBatch::SingleDelete(ColumnFamilyHandle* column_family,
                              const Slice& key) {
  WriteBatchInternal::SingleDelete(this, GetColumnFamilyID(column_family), key);
}

void WriteBatchInternal::SingleDelete(WriteBatch* b, uint32_t column_family_id,
                                      const SliceParts& key) {
  WriteBatchInternal::SetCount(b, WriteBatchInternal::Count(b) + 1);
  if (column_family_id == 0) {
    b->rep_.push_back(static_cast<char>(kTypeSingleDeletion));
  } else {
    b->rep_.push_back(static_cast<char>(kTypeColumnFamilySingleDeletion));
    PutVarint32(&b->rep_, column_family_id);
  }
  PutLengthPrefixedSliceParts(&b->rep_, key);
  b->content_flags_.store(b->content_flags_.load(std::memory_order_relaxed) |
                              ContentFlags::HAS_SINGLE_DELETE,
                          std::memory_order_relaxed);
}

void WriteBatch::SingleDelete(ColumnFamilyHandle* column_family,
                              const SliceParts& key) {
  WriteBatchInternal::SingleDelete(this, GetColumnFamilyID(column_family), key);
}

void WriteBatchInternal::Merge(WriteBatch* b, uint32_t column_family_id,
                               const Slice& key, const Slice& value) {
  WriteBatchInternal::SetCount(b, WriteBatchInternal::Count(b) + 1);
  if (column_family_id == 0) {
    b->rep_.push_back(static_cast<char>(kTypeMerge));
  } else {
    b->rep_.push_back(static_cast<char>(kTypeColumnFamilyMerge));
    PutVarint32(&b->rep_, column_family_id);
  }
  PutLengthPrefixedSlice(&b->rep_, key);
  PutLengthPrefixedSlice(&b->rep_, value);
  b->content_flags_.store(b->content_flags_.load(std::memory_order_relaxed) |
                              ContentFlags::HAS_MERGE,
                          std::memory_order_relaxed);
}

void WriteBatch::Merge(ColumnFamilyHandle* column_family, const Slice& key,
                       const Slice& value) {
  WriteBatchInternal::Merge(this, GetColumnFamilyID(column_family), key, value);
}

void WriteBatchInternal::Merge(WriteBatch* b, uint32_t column_family_id,
                               const SliceParts& key,
                               const SliceParts& value) {
  WriteBatchInternal::SetCount(b, WriteBatchInternal::Count(b) + 1);
  if (column_family_id == 0) {
    b->rep_.push_back(static_cast<char>(kTypeMerge));
  } else {
    b->rep_.push_back(static_cast<char>(kTypeColumnFamilyMerge));
    PutVarint32(&b->rep_, column_family_id);
  }
  PutLengthPrefixedSliceParts(&b->rep_, key);
  PutLengthPrefixedSliceParts(&b->rep_, value);
  b->content_flags_.store(b->content_flags_.load(std::memory_order_relaxed) |
                              ContentFlags::HAS_MERGE,
                          std::memory_order_relaxed);
}

void WriteBatch::Merge(ColumnFamilyHandle* column_family,
                       const SliceParts& key,
                       const SliceParts& value) {
  WriteBatchInternal::Merge(this, GetColumnFamilyID(column_family),
                            key, value);
}

void WriteBatch::PutLogData(const Slice& blob) {
  rep_.push_back(static_cast<char>(kTypeLogData));
  PutLengthPrefixedSlice(&rep_, blob);
}

void WriteBatch::SetSavePoint() {
  if (save_points_ == nullptr) {
    save_points_.reset(new SavePoints());
  }
  // Record length and count of current batch of writes.
  save_points_->stack.push(
     {GetDataSize(), Count(), content_flags_.load(std::memory_order_relaxed), frontiers_});
}

Status WriteBatch::RollbackToSavePoint() {
  if (save_points_ == nullptr || save_points_->stack.size() == 0) {
    return STATUS(NotFound, "");
  }

  // Pop the most recent savepoint off the stack
  SavePoint savepoint = save_points_->stack.top();
  save_points_->stack.pop();

  DCHECK_LE(savepoint.size, rep_.size());
  DCHECK_LE(savepoint.count, Count());

  if (savepoint.size == rep_.size()) {
    // No changes to rollback
  } else if (savepoint.size == 0) {
    // Rollback everything
    Clear();
  } else {
    rep_.resize(savepoint.size);
    WriteBatchInternal::SetCount(this, savepoint.count);
    content_flags_.store(savepoint.content_flags, std::memory_order_relaxed);
  }
  frontiers_ = savepoint.frontiers;

  return Status::OK();
}

namespace {

YB_STRONGLY_TYPED_BOOL(InMemoryErase);

class MemTableInserter : public WriteBatch::Handler {
 public:
  SequenceNumber sequence_;
  ColumnFamilyMemTables* const cf_mems_;
  FlushScheduler* const flush_scheduler_;
  const bool ignore_missing_column_families_;
  const uint64_t log_number_;
  DBImpl* db_;
  const InsertFlags insert_flags_;

  // cf_mems should not be shared with concurrent inserters
  MemTableInserter(SequenceNumber sequence, ColumnFamilyMemTables* cf_mems,
                   FlushScheduler* flush_scheduler,
                   bool ignore_missing_column_families, uint64_t log_number,
                   DB* db, InsertFlags insert_flags)
      : sequence_(sequence),
        cf_mems_(cf_mems),
        flush_scheduler_(flush_scheduler),
        ignore_missing_column_families_(ignore_missing_column_families),
        log_number_(log_number),
        db_(reinterpret_cast<DBImpl*>(db)),
        insert_flags_(insert_flags) {
    assert(cf_mems_);
    if (insert_flags_.Test(InsertFlag::kFilterDeletes)) {
      assert(db_);
    }
  }

  bool SeekToColumnFamily(uint32_t column_family_id, Status* s) {
    // If we are in a concurrent mode, it is the caller's responsibility
    // to clone the original ColumnFamilyMemTables so that each thread
    // has its own instance.  Otherwise, it must be guaranteed that there
    // is no concurrent access
    bool found = cf_mems_->Seek(column_family_id);
    if (!found) {
      if (ignore_missing_column_families_) {
        *s = Status::OK();
      } else {
        *s = STATUS(InvalidArgument,
            "Invalid column family specified in write batch");
      }
      return false;
    }
    if (log_number_ != 0 && log_number_ < cf_mems_->GetLogNumber()) {
      // This is true only in recovery environment (log_number_ is always 0 in
      // non-recovery, regular write code-path)
      // * If log_number_ < cf_mems_->GetLogNumber(), this means that column
      // family already contains updates from this log. We can't apply updates
      // twice because of update-in-place or merge workloads -- ignore the
      // update
      *s = Status::OK();
      return false;
    }
    return true;
  }

  Status PutCF(
      uint32_t column_family_id, const SliceParts& key, const SliceParts& value) override {
    Status seek_status;
    if (!SeekToColumnFamily(column_family_id, &seek_status)) {
      ++sequence_;
      return seek_status;
    }
    MemTable* mem = cf_mems_->GetMemTable();
    auto* moptions = mem->GetMemTableOptions();
    if (!moptions->inplace_update_support) {
      mem->Add(CurrentSequenceNumber(), kTypeValue, key, value,
               insert_flags_.Test(InsertFlag::kConcurrentMemtableWrites));
    } else if (moptions->inplace_callback == nullptr) {
      assert(!insert_flags_.Test(InsertFlag::kConcurrentMemtableWrites));
      mem->Update(CurrentSequenceNumber(), key.TheOnlyPart(), value.TheOnlyPart());
      RecordTick(moptions->statistics, NUMBER_KEYS_UPDATED);
    } else {
      assert(!insert_flags_.Test(InsertFlag::kConcurrentMemtableWrites));
      SequenceNumber current_seq = CurrentSequenceNumber();
      if (mem->UpdateCallback(current_seq, key.TheOnlyPart(), value.TheOnlyPart())) {
      } else {
        // key not found in memtable. Do sst get, update, add
        SnapshotImpl read_from_snapshot;
        read_from_snapshot.number_ = current_seq;
        ReadOptions ropts;
        ropts.snapshot = &read_from_snapshot;

        std::string prev_value;
        std::string merged_value;

        auto cf_handle = cf_mems_->GetColumnFamilyHandle();
        if (cf_handle == nullptr) {
          cf_handle = db_->DefaultColumnFamily();
        }
        Status s = db_->Get(ropts, cf_handle, key.TheOnlyPart(), &prev_value);

        char* prev_buffer = const_cast<char*>(prev_value.c_str());
        uint32_t prev_size = static_cast<uint32_t>(prev_value.size());
        auto status = moptions->inplace_callback(s.ok() ? prev_buffer : nullptr,
                                                 s.ok() ? &prev_size : nullptr,
                                                 value.TheOnlyPart(), &merged_value);
        if (status == UpdateStatus::UPDATED_INPLACE) {
          // prev_value is updated in-place with final value.
          Slice new_value(prev_buffer, prev_size);
          mem->Add(current_seq, kTypeValue, key, SliceParts(&new_value, 1));
          RecordTick(moptions->statistics, NUMBER_KEYS_WRITTEN);
        } else if (status == UpdateStatus::UPDATED) {
          // merged_value contains the final value.
          Slice new_value(merged_value);
          mem->Add(current_seq, kTypeValue, key, SliceParts(&new_value, 1));
          RecordTick(moptions->statistics, NUMBER_KEYS_WRITTEN);
        }
      }
    }
    // Since all Puts are logged in transaction logs (if enabled), always bump
    // sequence number. Even if the update eventually fails and does not result
    // in memtable add/update.
    sequence_++;
    CheckMemtableFull();
    return Status::OK();
  }

  Status DeleteImpl(uint32_t column_family_id, const Slice& key,
                    ValueType delete_type) {
    Status seek_status;
    if (!SeekToColumnFamily(column_family_id, &seek_status)) {
      ++sequence_;
      return seek_status;
    }
    MemTable* mem = cf_mems_->GetMemTable();
    if ((delete_type == ValueType::kTypeSingleDeletion ||
         delete_type == ValueType::kTypeColumnFamilySingleDeletion) &&
        mem->Erase(key)) {
      return Status::OK();
    }
    auto* moptions = mem->GetMemTableOptions();
    if (insert_flags_.Test(InsertFlag::kFilterDeletes) && moptions->filter_deletes) {
      assert(!insert_flags_.Test(InsertFlag::kConcurrentMemtableWrites));
      SnapshotImpl read_from_snapshot;
      read_from_snapshot.number_ = sequence_;
      ReadOptions ropts;
      ropts.snapshot = &read_from_snapshot;
      std::string value;
      auto cf_handle = cf_mems_->GetColumnFamilyHandle();
      if (cf_handle == nullptr) {
        cf_handle = db_->DefaultColumnFamily();
      }
      if (!db_->KeyMayExist(ropts, cf_handle, key, &value)) {
        RecordTick(moptions->statistics, NUMBER_FILTERED_DELETES);
        return Status::OK();
      }
    }
    mem->Add(CurrentSequenceNumber(), delete_type, SliceParts(&key, 1), SliceParts(),
             insert_flags_.Test(InsertFlag::kConcurrentMemtableWrites));
    sequence_++;
    CheckMemtableFull();
    return Status::OK();
  }

  virtual Status DeleteCF(uint32_t column_family_id,
                                  const Slice& key) override {
    return DeleteImpl(column_family_id, key, kTypeDeletion);
  }

  virtual Status SingleDeleteCF(uint32_t column_family_id,
                                        const Slice& key) override {
    return DeleteImpl(column_family_id, key, kTypeSingleDeletion);
  }

  virtual Status MergeCF(uint32_t column_family_id, const Slice& key,
                                 const Slice& value) override {
    assert(!insert_flags_.Test(InsertFlag::kConcurrentMemtableWrites));
    Status seek_status;
    if (!SeekToColumnFamily(column_family_id, &seek_status)) {
      ++sequence_;
      return seek_status;
    }
    MemTable* mem = cf_mems_->GetMemTable();
    auto* moptions = mem->GetMemTableOptions();
    bool perform_merge = false;

    SequenceNumber current_seq = CurrentSequenceNumber();
    if (moptions->max_successive_merges > 0 && db_ != nullptr) {
      LookupKey lkey(key, current_seq);

      // Count the number of successive merges at the head
      // of the key in the memtable
      size_t num_merges = mem->CountSuccessiveMergeEntries(lkey);

      if (num_merges >= moptions->max_successive_merges) {
        perform_merge = true;
      }
    }

    if (perform_merge) {
      // 1) Get the existing value
      std::string get_value;

      // Pass in the sequence number so that we also include previous merge
      // operations in the same batch.
      SnapshotImpl read_from_snapshot;
      read_from_snapshot.number_ = current_seq;
      ReadOptions read_options;
      read_options.snapshot = &read_from_snapshot;

      auto cf_handle = cf_mems_->GetColumnFamilyHandle();
      if (cf_handle == nullptr) {
        cf_handle = db_->DefaultColumnFamily();
      }
      RETURN_NOT_OK(db_->Get(read_options, cf_handle, key, &get_value));
      Slice get_value_slice = Slice(get_value);

      // 2) Apply this merge
      auto merge_operator = moptions->merge_operator;
      assert(merge_operator);

      std::deque<std::string> operands;
      operands.push_front(value.ToString());
      std::string new_value;
      bool merge_success = false;
      {
        StopWatchNano timer(Env::Default(), moptions->statistics != nullptr);
        PERF_TIMER_GUARD(merge_operator_time_nanos);
        merge_success = merge_operator->FullMerge(
            key, &get_value_slice, operands, &new_value, moptions->info_log);
        RecordTick(moptions->statistics, MERGE_OPERATION_TOTAL_TIME,
                   timer.ElapsedNanos());
      }

      if (!merge_success) {
          // Failed to merge!
        RecordTick(moptions->statistics, NUMBER_MERGE_FAILURES);

        // Store the delta in memtable
        perform_merge = false;
      } else {
        // 3) Add value to memtable
        Slice value_slice(new_value);
        mem->Add(current_seq, kTypeValue, SliceParts(&key, 1), SliceParts(&value_slice, 1));
      }
    }

    if (!perform_merge) {
      // Add merge operator to memtable
      mem->Add(current_seq, kTypeMerge, SliceParts(&key, 1), SliceParts(&value, 1));
    }

    sequence_++;
    CheckMemtableFull();
    return Status::OK();
  }

  Status Frontiers(const UserFrontiers& frontiers) override {
    Status seek_status;
    if (!SeekToColumnFamily(0, &seek_status)) {
      return seek_status;
    }
    cf_mems_->GetMemTable()->UpdateFrontiers(frontiers);
    return Status::OK();
  }

  void CheckMemtableFull() {
    if (flush_scheduler_ != nullptr) {
      auto* cfd = cf_mems_->current();
      assert(cfd != nullptr);
      if (cfd->mem()->ShouldScheduleFlush() &&
          cfd->mem()->MarkFlushScheduled()) {
        // MarkFlushScheduled only returns true if we are the one that
        // should take action, so no need to dedup further
        flush_scheduler_->ScheduleFlush(cfd);
      }
    }
  }

 private:
  SequenceNumber CurrentSequenceNumber() {
    return sequence_;
  }
};

}  // namespace

// This function can only be called in these conditions:
// 1) During Recovery()
// 2) During Write(), in a single-threaded write thread
// 3) During Write(), in a concurrent context where memtables has been cloned
// The reason is that it calls memtables->Seek(), which has a stateful cache
Status WriteBatchInternal::InsertInto(
    const autovector<WriteThread::Writer*>& writers, SequenceNumber sequence,
    ColumnFamilyMemTables* memtables, FlushScheduler* flush_scheduler,
    bool ignore_missing_column_families, uint64_t log_number, DB* db,
    InsertFlags insert_flags) {
  MemTableInserter inserter(sequence, memtables, flush_scheduler,
                            ignore_missing_column_families, log_number, db,
                            insert_flags);

  for (size_t i = 0; i < writers.size(); i++) {
    if (!writers[i]->CallbackFailed()) {
      writers[i]->status = writers[i]->batch->Iterate(&inserter);
      if (!writers[i]->status.ok()) {
        return writers[i]->status;
      }
    }
  }
  return Status::OK();
}

Status WriteBatchInternal::InsertInto(const WriteBatch* batch,
                                      ColumnFamilyMemTables* memtables,
                                      FlushScheduler* flush_scheduler,
                                      bool ignore_missing_column_families,
                                      uint64_t log_number, DB* db,
                                      InsertFlags insert_flags) {
  MemTableInserter inserter(WriteBatchInternal::Sequence(batch), memtables,
                            flush_scheduler, ignore_missing_column_families,
                            log_number, db, insert_flags);
  return batch->Iterate(&inserter);
}

void WriteBatchInternal::SetContents(WriteBatch* b, const Slice& contents) {
  DCHECK_GE(contents.size(), kHeader);
  b->rep_.assign(contents.cdata(), contents.size());
  b->content_flags_.store(ContentFlags::DEFERRED, std::memory_order_relaxed);
}

void WriteBatchInternal::Append(WriteBatch* dst, const WriteBatch* src) {
  SetCount(dst, Count(dst) + Count(src));
  DCHECK_GE(src->rep_.size(), kHeader);
  dst->rep_.append(src->rep_.data() + kHeader, src->rep_.size() - kHeader);
  dst->content_flags_.store(
      dst->content_flags_.load(std::memory_order_relaxed) |
          src->content_flags_.load(std::memory_order_relaxed),
      std::memory_order_relaxed);
}

size_t WriteBatchInternal::AppendedByteSize(size_t leftByteSize,
                                            size_t rightByteSize) {
  if (leftByteSize == 0 || rightByteSize == 0) {
    return leftByteSize + rightByteSize;
  } else {
    return leftByteSize + rightByteSize - kHeader;
  }
}

Result<size_t> DirectInsert(
    WriteBatch::Handler* handler, DirectWriter* writer, WriteBatch::Handler* handler_for_logging) {
  auto mem_table_inserter = down_cast<MemTableInserter*>(handler);
  auto* mems = mem_table_inserter->cf_mems_;
  auto current = mems->current();
  if (!current) {
    mems->Seek(0);
    current = mems->current();
  }
  DirectWriteHandlerImpl direct_write_handler(
      current->mem(), mem_table_inserter->sequence_, handler_for_logging);
  RETURN_NOT_OK(writer->Apply(&direct_write_handler));
  auto result = direct_write_handler.Complete();
  mem_table_inserter->CheckMemtableFull();
  return result;
}

}  // namespace rocksdb
