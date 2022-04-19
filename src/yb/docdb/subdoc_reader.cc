// Copyright (c) YugaByte, Inc.
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

#include "yb/docdb/subdoc_reader.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <glog/logging.h>

#include "yb/common/doc_hybrid_time.h"
#include "yb/common/hybrid_time.h"
#include "yb/common/transaction.h"
#include "yb/common/typedefs.h"

#include "yb/docdb/deadline_info.h"
#include "yb/docdb/doc_key.h"
#include "yb/docdb/doc_ttl_util.h"
#include "yb/docdb/expiration.h"
#include "yb/docdb/intent_aware_iterator.h"
#include "yb/docdb/key_bytes.h"
#include "yb/docdb/packed_row.h"
#include "yb/docdb/primitive_value.h"
#include "yb/docdb/subdocument.h"
#include "yb/docdb/value.h"
#include "yb/docdb/value_type.h"

#include "yb/gutil/integral_types.h"
#include "yb/gutil/macros.h"

#include "yb/server/hybrid_clock.h"

#include "yb/util/fast_varint.h"
#include "yb/util/monotime.h"
#include "yb/util/result.h"
#include "yb/util/status.h"
#include "yb/util/status_format.h"

using std::vector;

using yb::HybridTime;

namespace yb {
namespace docdb {

namespace {

Expiration GetNewExpiration(
    const Expiration& parent_exp, const MonoDelta& ttl,
    const DocHybridTime& new_write_time) {
  Expiration new_exp = parent_exp;
  // We may need to update the TTL in individual columns.
  if (new_write_time.hybrid_time() >= new_exp.write_ht) {
    // We want to keep the default TTL otherwise.
    if (ttl != ValueControlFields::kMaxTtl) {
      new_exp.write_ht = new_write_time.hybrid_time();
      new_exp.ttl = ttl;
    } else if (new_exp.ttl.IsNegative()) {
      new_exp.ttl = -new_exp.ttl;
    }
  }

  // If the hybrid time is kMin, then we must be using default TTL.
  if (new_exp.write_ht == HybridTime::kMin) {
    new_exp.write_ht = new_write_time.hybrid_time();
  }

  return new_exp;
}

} // namespace

ObsolescenceTracker::ObsolescenceTracker(DocHybridTime write_time_watermark):
    write_time_watermark_(write_time_watermark) {}

ObsolescenceTracker::ObsolescenceTracker(
    const ReadHybridTime& read_time, DocHybridTime write_time_watermark, Expiration expiration):
    write_time_watermark_(write_time_watermark), read_time_(read_time), expiration_(expiration) {}

const DocHybridTime& ObsolescenceTracker::GetHighWriteTime() { return write_time_watermark_; }

bool ObsolescenceTracker::IsObsolete(const DocHybridTime& write_time) const {
  if (expiration_.has_value()) {
    DCHECK(read_time_.has_value());
    if (HasExpiredTTL(expiration_.value().write_ht, expiration_.value().ttl,
                      read_time_.value().read)) {
      return true;
    }
  }
  return write_time < write_time_watermark_;
}

ObsolescenceTracker ObsolescenceTracker::Child(
    const DocHybridTime& write_time, const MonoDelta& ttl) const {
  auto new_write_time_watermark = std::max(write_time, write_time_watermark_);
  if (expiration_.has_value()) {
    DCHECK(read_time_.has_value());
    return ObsolescenceTracker(
        read_time_.value(), new_write_time_watermark,
        GetNewExpiration(expiration_.value(), ttl, write_time));
  }
  return ObsolescenceTracker(new_write_time_watermark);
}

ObsolescenceTracker ObsolescenceTracker::Child(const DocHybridTime& write_time) const {
  auto new_write_time_watermark = std::max(write_time, write_time_watermark_);
  if (expiration_.has_value()) {
    return ObsolescenceTracker(read_time_.value(), new_write_time_watermark, expiration_.get());
  }
  return ObsolescenceTracker(new_write_time_watermark);
}

boost::optional<uint64_t> ObsolescenceTracker::GetTtlRemainingSeconds(
    const HybridTime& ttl_write_time) const {
  if (!expiration_.has_value()) {
    return boost::none;
  }

  DCHECK(read_time_.has_value());

  auto ttl_value = expiration_.value().ttl;
  auto doc_read_time = read_time_.value();

  if (ttl_value == ValueControlFields::kMaxTtl) {
    return -1;
  }
  int64_t time_since_ttl_write_seconds = (
      server::HybridClock::GetPhysicalValueMicros(doc_read_time.read) -
      server::HybridClock::GetPhysicalValueMicros(ttl_write_time)) /
      MonoTime::kMicrosecondsPerSecond;
  int64_t ttl_value_seconds = ttl_value.ToMilliseconds() / MonoTime::kMillisecondsPerSecond;
  int64_t ttl_remaining_seconds = ttl_value_seconds - time_since_ttl_write_seconds;
  return std::max(static_cast<int64_t>(0), ttl_remaining_seconds);
}

namespace {

// This class wraps access to a SubDocument instance specified in one of two ways:
// (1) -- From a pointer to an already existing instance of a SubDocument
// (2) -- From a pointer to an existing instance of LazySubDocumentHolder which represents the
//        "parent" of the SubDocument accessed via this LazySubDocumentHolder.
//
// Using this class, a SubDocument which is *specified* via (2) above will not be constructed unless
// it is accessed, either directly or via a child LazySubDocumentHolder instance. This class allows
// us to provide a unified interface to the reading/constructing code which may determine if a
// particular SubDocument is live based on deferred knowledge of whether any of it's children are
// live.
class LazySubDocumentHolder {
 public:
  // Specifies the SubDocument "target" provided. The pointer must be valid for the lifetime of this
  // instance. The provided key Slice must be valid and the underlying data should not change during
  // the lifetime of this instance.
  LazySubDocumentHolder(SubDocument* target, Slice key) : target_(target), key_(key) {
    DCHECK(target_);
  }

  // Specifies a SubDocument with key "key" lazily constructed from "parent". Provided parent must
  // be valid and remain at the same address for the lifetime of this instance. The provided key
  // Slice must be valid and the underlying data should not change during the lifetime of this
  // instance.
  LazySubDocumentHolder(LazySubDocumentHolder* parent, Slice key) : parent_(parent), key_(key) {
    DCHECK(parent_);
  }

  // Returns true if the SubDocument specified by this instance exists.
  bool IsConstructed() const { return target_; }

  // Get a pointer to the SubDocument specified by this instance. Construct that SubDocument first
  // if it has not yet been constructed.
  Result<SubDocument*> Get();

 private:
  // The constructed SubDocument specified by this instance.
  SubDocument* target_ = nullptr;

  // A pointer to the parent_ of the specified SubDocument. This must be non-null unless this
  // instance was constructed via a concrete SubDocument*.
  LazySubDocumentHolder* const parent_ = nullptr;

  // The key of the specified SubDocument.
  Slice key_;
};

Result<SubDocument*> LazySubDocumentHolder::Get() {
  // If target_ is not null, just return it. Otherwise, we must construct the SubDocument from its
  // parent.
  if (target_) {
    return target_;
  }

  // Presumably, the parent_ key is a prefix of key_, otherwise it's not a valid parent.
  DCHECK(key_.starts_with(parent_->key_))
      << "Attempting to construct SubDocument for key: " << SubDocKey::DebugSliceToString(key_)
      << " from parent whose key: " << SubDocKey::DebugSliceToString(parent_->key_)
      << " is not a prefix of child key";
  // This code takes each subdoc key part after parent_ in key_ and traverses from the parent_
  // SubDocument to a SubDocument created via successive calls to GetOrAddChild. The SubDocument
  // returned is owned by and borrowed from parent_.
  SubDocument* current = VERIFY_RESULT(parent_->Get());
  Slice temp = key_;
  temp.remove_prefix(parent_->key_.size());
  for (;;) {
    KeyEntryValue child_key_part;
    RETURN_NOT_OK(child_key_part.DecodeFromKey(&temp));
    current = current->GetOrAddChild(child_key_part).first;
    if (temp.empty()) {
      target_ = current;
      return target_;
    }
  }
  return STATUS(
      InternalError,
      "We return this status at the end of a function with a terminal infinite loop. We should "
      "never get here.");
}

// This class provides a wrapper to access data corresponding to a RocksDB row.
class DocDbRowData {
 public:
  DocDbRowData(const Slice& key, const DocHybridTime& write_time);

  static Result<std::unique_ptr<DocDbRowData>> CurrentRow(
      IntentAwareIterator* iter, const PackedColumnData& packed_column_data,
      const KeyBytes* packed_row_key);

  const KeyBytes& key() const { return target_key_; }

  const Value& value() const { return value_; }

  Value& value() { return value_; }

  const DocHybridTime& write_time() const { return write_time_; }

  bool IsTombstone() const { return value_.value_type() == ValueEntryType::kTombstone; }

  bool IsCollection() const { return IsCollectionType(value_.value_type()); }

  bool IsPrimitiveValue() const { return IsPrimitiveValueType(value_.value_type()); }

  void InitWriteTime();

 private:
  const KeyBytes target_key_;
  const DocHybridTime write_time_;
  Value value_;

  DISALLOW_COPY_AND_ASSIGN(DocDbRowData);
};

DocDbRowData::DocDbRowData(
    const Slice& key, const DocHybridTime& write_time):
    target_key_(std::move(key)), write_time_(std::move(write_time)) {}

Result<std::unique_ptr<DocDbRowData>> DocDbRowData::CurrentRow(
    IntentAwareIterator* iter, const PackedColumnData& packed_column_data,
    const KeyBytes* packed_row_key) {
  std::unique_ptr<DocDbRowData> result;

  if (iter->valid()) {
    auto key_data = VERIFY_RESULT(iter->FetchKey());
    DCHECK(key_data.same_transaction ||
        iter->read_time().global_limit >= key_data.write_time.hybrid_time())
        << "Bad key: " << SubDocKey::DebugSliceToString(key_data.key)
        << ", global limit: " << iter->read_time().global_limit
        << ", write time: " << key_data.write_time.hybrid_time();

    if (key_data.write_time == DocHybridTime::kMin) {
      return STATUS(Corruption, "No hybrid timestamp found on entry");
    }

    if (!packed_column_data || packed_column_data.row->doc_ht < key_data.write_time) {
      result = std::make_unique<DocDbRowData>(key_data.key, key_data.write_time);
      // TODO -- we could optimize be decoding directly into a SubDocument instance on the heap
      // which could be later bound to our result SubDocument. This could work if e.g. Value could
      // be initialized with a PrimitiveValue*.
      RETURN_NOT_OK(result->value().Decode(iter->value()));
    }
  }

  if (!result) {
    if (!packed_column_data) {
      return STATUS(
          InternalError, "Invalid iter without packed column data in DocDbRowData::CurrentRow");
    }
    result = std::make_unique<DocDbRowData>(*packed_row_key, packed_column_data.row->doc_ht);
    RETURN_NOT_OK(result->value().Decode(
        packed_column_data.slice, packed_column_data.row->control_fields));
  }

  return result;
}

void DocDbRowData::InitWriteTime() {
  if (value_.has_user_timestamp()) {
    value_.mutable_primitive_value()->SetWriteTime(value_.user_timestamp());
  } else {
    value_.mutable_primitive_value()->SetWriteTime(
        write_time_.hybrid_time().GetPhysicalValueMicros());
  }
}

// This class provides a convenience handle for modifying a SubDocument specified by a provided
// LazySubDocumentHolder. Importantly, it is responsible for the semantics of when a
// LazySubDocumentHolder should be realized. Notably, it does *not* construct the specified
// SubDocument if it is instructed to store a tombstone, and it will only modify the SubDocument
// value if it already has been constructed.
class DocDbRowAssembler {
 public:
  DocDbRowAssembler(SubDocument* target, Slice key) : root_(target, key) {}

  DocDbRowAssembler(DocDbRowAssembler* parent_assembler, Slice key):
      root_(&parent_assembler->root_, key) {}

  CHECKED_STATUS SetEmptyCollection();

  CHECKED_STATUS SetTombstone();

  CHECKED_STATUS SetPrimitiveValue(DocDbRowData* row);

  Result<bool> HasStoredValue();

 private:
  LazySubDocumentHolder root_;
  bool is_tombstoned_ = false;

  DISALLOW_COPY_AND_ASSIGN(DocDbRowAssembler);
};

Status DocDbRowAssembler::SetEmptyCollection() {
  auto* subdoc = VERIFY_RESULT(root_.Get());
  *subdoc = SubDocument();
  return Status::OK();
}

Status DocDbRowAssembler::SetTombstone() {
  if (!root_.IsConstructed()) {
    // Do not construct a child subdocument from the parent if it is not constructed, since this is
    // a tombstone.
    return Status::OK();
  }
  auto* subdoc = VERIFY_RESULT(root_.Get());
  *subdoc = SubDocument(ValueEntryType::kTombstone);
  is_tombstoned_ = true;
  return Status::OK();
}

Status DocDbRowAssembler::SetPrimitiveValue(DocDbRowData* row) {
  // TODO -- this interface with a non-const row pointer is not ideal. It's awkward to allow the
  // DocDbRowAssembler to modify the DocDbRowData's state. In the future, it might make more
  // sense to have ScopedDocDbRowContext orchestrate coordination between these classes.
  // TODO -- we currently modify the DocDbRowData's mutable primitive_value, and then make
  // a copy here to store onto the SubDocument. This should be made more efficient, especially
  // since it's on the critical path of all reads.
  auto* subdoc = VERIFY_RESULT(root_.Get());

  row->InitWriteTime();
  *subdoc = SubDocument(std::move(*row->value().mutable_primitive_value()));
  return Status::OK();
}

Result<bool> DocDbRowAssembler::HasStoredValue() {
  if (!root_.IsConstructed()) {
    return false;
  }
  auto* subdoc = VERIFY_RESULT(root_.Get());
  return subdoc->value_type() != ValueEntryType::kInvalid
      && subdoc->value_type() != ValueEntryType::kTombstone;
}

class ScopedDocDbRowContext;
class ScopedDocDbRowContextWithData;

// This class represents a collection of ScopedDocDbRowContext instances corresponding to a DocDB
// collection. The user of this class can optionally call SetFirstChild on an owned
// ScopedDocDbRowContext in case it has already read the first row of this collection, and that will
// be returned before reading subsequent rows in the collection. Note this may only be done before
// the ScopedDocDbCollectionContext instance has read any other rows.
class ScopedDocDbCollectionContext {
 public:
  explicit ScopedDocDbCollectionContext(ScopedDocDbRowContext* parent);

  CHECKED_STATUS SetFirstChild(std::unique_ptr<DocDbRowData> first_row);

  Result<ScopedDocDbRowContextWithData*> GetNextChild();

 private:
  void SetNextChild(std::unique_ptr<DocDbRowData> child_row);

  ScopedDocDbRowContext* const parent_ = nullptr;
  std::unique_ptr<ScopedDocDbRowContextWithData> current_child_ = nullptr;
  ScopedDocDbRowContextWithData* next_child_ = nullptr;
};

// This class encapsulates all relevant context for reading the RocksDB state corresponding to a
// particular key and constructing a SubDocument instance which reflects that state. This context is
// used by control-flow functions at the bottom of this file. The context for a key also
// encapsulates a mechanism to collect the context for any children of the same key via
// ScopedDocDbCollectionContext, which is similarly used by control-flow functions at the bottom of
// this file.
class ScopedDocDbRowContext {
 public:
  ScopedDocDbRowContext(
      IntentAwareIterator* iter,
      DeadlineInfo* deadline_info,
      Slice key,
      SubDocument* assembly_target,
      ObsolescenceTracker obsolescence_tracker);

  ScopedDocDbRowContext(
      IntentAwareIterator* iter,
      DeadlineInfo* deadline_info,
      Slice key,
      DocDbRowAssembler* ancestor_assembler,
      ObsolescenceTracker obsolescence_tracker);

  DocDbRowAssembler* mutable_assembler() { return &assembler_; }

  const ObsolescenceTracker* obsolescence_tracker() const { return &obsolescence_tracker_; }

  ScopedDocDbCollectionContext* collection();

  CHECKED_STATUS CheckDeadline();

 protected:
  IntentAwareIterator* const iter_;
  DeadlineInfo* const deadline_info_;
  const Slice key_;
  const IntentAwareIteratorPrefixScope prefix_scope_;
  DocDbRowAssembler assembler_;
  ObsolescenceTracker obsolescence_tracker_;
  boost::optional<ScopedDocDbCollectionContext> collection_ = boost::none;

 private:
  friend class ScopedDocDbCollectionContext;

  DISALLOW_COPY_AND_ASSIGN(ScopedDocDbRowContext);
};

ScopedDocDbRowContext::ScopedDocDbRowContext(
      IntentAwareIterator* iter,
      DeadlineInfo* deadline_info,
      Slice key,
      SubDocument* assembly_target,
      ObsolescenceTracker obsolescence_tracker):
    iter_(iter),
    deadline_info_(deadline_info),
    key_(key),
    prefix_scope_(key_, iter_),
    assembler_(assembly_target, key_),
    obsolescence_tracker_(obsolescence_tracker) {}

ScopedDocDbRowContext::ScopedDocDbRowContext(
      IntentAwareIterator* iter,
      DeadlineInfo* deadline_info,
      Slice key,
      DocDbRowAssembler* ancestor_assembler,
      ObsolescenceTracker obsolescence_tracker):
    iter_(iter),
    deadline_info_(deadline_info),
    key_(key),
    prefix_scope_(key_, iter_),
    assembler_(ancestor_assembler, key_),
    obsolescence_tracker_(obsolescence_tracker) {}

ScopedDocDbCollectionContext* ScopedDocDbRowContext::collection() {
  if (collection_ == boost::none) {
    iter_->SeekPastSubKey(key_);
    collection_.emplace(this);
  }
  return &*collection_;
}

Status ScopedDocDbRowContext::CheckDeadline() {
  if (deadline_info_ && deadline_info_->CheckAndSetDeadlinePassed()) {
    return STATUS(Expired, "Deadline for query passed.");
  }
  return Status::OK();
}

class ScopedDocDbRowContextWithData : public ScopedDocDbRowContext {
 public:
  ScopedDocDbRowContextWithData(
      std::unique_ptr<DocDbRowData> row,
      IntentAwareIterator* iter,
      DeadlineInfo* deadline_info,
      SubDocument* assembly_target,
      ObsolescenceTracker ancestor_obsolescence_tracker);

  ScopedDocDbRowContextWithData(
      std::unique_ptr<DocDbRowData> row,
      IntentAwareIterator* iter,
      DeadlineInfo* deadline_info,
      DocDbRowAssembler* ancestor_assembler,
      ObsolescenceTracker ancestor_obsolescence_tracker);

  DocDbRowData* data() const { return data_.get(); }

  void SeekOutOfPrefix();

 private:
  std::unique_ptr<DocDbRowData> data_;

  DISALLOW_COPY_AND_ASSIGN(ScopedDocDbRowContextWithData);
};

ScopedDocDbRowContextWithData::ScopedDocDbRowContextWithData(
    std::unique_ptr<DocDbRowData> row,
    IntentAwareIterator* iter,
    DeadlineInfo* deadline_info,
    SubDocument* assembly_target,
    ObsolescenceTracker ancestor_obsolescence_tracker):
    ScopedDocDbRowContext(
        iter, deadline_info, row->key(), assembly_target,
        ancestor_obsolescence_tracker.Child(row->write_time(), row->value().ttl())),
    data_(std::move(row)) {}

ScopedDocDbRowContextWithData::ScopedDocDbRowContextWithData(
    std::unique_ptr<DocDbRowData> row,
    IntentAwareIterator* iter,
    DeadlineInfo* deadline_info,
    DocDbRowAssembler* ancestor_assembler,
    ObsolescenceTracker ancestor_obsolescence_tracker):
    ScopedDocDbRowContext(
        iter, deadline_info, row->key(), ancestor_assembler,
        ancestor_obsolescence_tracker.Child(row->write_time(), row->value().ttl())),
    data_(std::move(row)) {}

void ScopedDocDbRowContextWithData::SeekOutOfPrefix() {
  iter_->SeekOutOfSubDoc(data_->key());
}

ScopedDocDbCollectionContext::ScopedDocDbCollectionContext(ScopedDocDbRowContext* parent):
    parent_(parent) {}

Status ScopedDocDbCollectionContext::SetFirstChild(std::unique_ptr<DocDbRowData> first_row) {
  if (next_child_) {
    return STATUS(IllegalState, "Cannot set first_child if already set.");
  }
  if (current_child_) {
    return STATUS(IllegalState, "Cannot set first_child if a child has already been read.");
  }
  SetNextChild(std::move(first_row));
  next_child_ = current_child_.get();
  return Status::OK();
}

Result<ScopedDocDbRowContextWithData*> ScopedDocDbCollectionContext::GetNextChild() {
  if (next_child_) {
    // If there is a next_child_, then we've already stored a row which we read before. Serve that,
    // and reset it to resume normal operation next time.
    next_child_ = nullptr;
  } else {
    if (current_child_) {
      // prev_child_ points to the row we served last, which we are now done with, so we should
      // seek out of the scope of it and reset its state before proceeding to read the next row.
      // Note -- we currently seek away from the previous row only when a new row is requested by
      // the caller. It might be better from a perf perspective to do this before instead.
      current_child_->SeekOutOfPrefix();

      // Reset current child to eliminate the IntentAwareIteratorPrefixScope it was holding.
      current_child_.reset();
    }
    if (parent_->iter_->valid()) {
      SetNextChild(VERIFY_RESULT(DocDbRowData::CurrentRow(
          parent_->iter_, PackedColumnData(), nullptr)));
    }
  }
  DCHECK(
      !current_child_ ||
      !parent_ ||
      current_child_->data()->key().AsSlice().starts_with(parent_->key_))
      << "Child key " << SubDocKey::DebugSliceToString(current_child_->data()->key().AsSlice())
      << " does not include parent key " << SubDocKey::DebugSliceToString(parent_->key_)
      << " as prefix.";
  return current_child_.get();
}

void ScopedDocDbCollectionContext::SetNextChild(std::unique_ptr<DocDbRowData> child_row) {
  current_child_ = std::make_unique<ScopedDocDbRowContextWithData>(
      std::move(child_row), parent_->iter_, parent_->deadline_info_, parent_->mutable_assembler(),
      parent_->obsolescence_tracker_);
}

CHECKED_STATUS ProcessSubDocument(ScopedDocDbRowContextWithData* scope);

Result<uint32_t> ProcessChildren(ScopedDocDbCollectionContext* collection) {
  uint32_t num_children = 0;
  if (collection) {
    while (ScopedDocDbRowContextWithData* child = VERIFY_RESULT(collection->GetNextChild())) {
      RETURN_NOT_OK(ProcessSubDocument(child));
      if (VERIFY_RESULT(child->mutable_assembler()->HasStoredValue())) {
        ++num_children;
      }
    }
  }
  return num_children;
}

Status ProcessCollection(ScopedDocDbRowContextWithData* scope) {
  // Set this row to an empty collection since it is alive/valid before processing its children.
  RETURN_NOT_OK(scope->mutable_assembler()->SetEmptyCollection());
  RETURN_NOT_OK(ProcessChildren(scope->collection()));
  return Status::OK();
}

Status MaybeReviveCollection(ScopedDocDbRowContextWithData* scope) {
  auto num_children = VERIFY_RESULT(ProcessChildren(scope->collection()));
  if (num_children == 0) {
    return scope->mutable_assembler()->SetTombstone();
  }
  return Status::OK();
}

Status ProcessSubDocument(ScopedDocDbRowContextWithData* scope) {
  RETURN_NOT_OK(scope->CheckDeadline());

  auto data = scope->data();
  auto assembler = scope->mutable_assembler();
  auto obsolescence_tracker = scope->obsolescence_tracker();

  if (data->IsTombstone() || obsolescence_tracker->IsObsolete(data->write_time())) {
    if (data->IsPrimitiveValue()) {
      VLOG(4) << "Discarding overwritten or expired primitive value";
      return assembler->SetTombstone();
    }
    // If the latest written value is a tombstone or the record is expired at a top level, only
    // surface a subdocument if it has a valid (unexpired, non-tombstoned) child which overwrites
    // this record. Note: these semantics are only relevant to CQL reads.
    return MaybeReviveCollection(scope);
  }

  if (data->IsCollection()) {
    return ProcessCollection(scope);
  }

  if (data->IsPrimitiveValue()) {
    auto ttl_opt = obsolescence_tracker->GetTtlRemainingSeconds(data->write_time().hybrid_time());
    if (ttl_opt) {
      data->value().mutable_primitive_value()->SetTtl(*ttl_opt);
    }
    return assembler->SetPrimitiveValue(data);
  }

  return STATUS_FORMAT(
      Corruption,
      "Expected primitive value type, collection, or tobmstone. Got $0",
      data->value().value_type());
}

}  // namespace

SubDocumentReader::SubDocumentReader(
    const KeyBytes& target_subdocument_key,
    IntentAwareIterator* iter,
    DeadlineInfo* deadline_info,
    const ObsolescenceTracker& ancestor_obsolescence_tracker)
    : target_subdocument_key_(target_subdocument_key), iter_(iter), deadline_info_(deadline_info),
      ancestor_obsolescence_tracker_(ancestor_obsolescence_tracker) {}

Status SubDocumentReader::Get(SubDocument* result, const PackedColumnData& packed_column_data) {
  IntentAwareIteratorPrefixScope target_scope(target_subdocument_key_, iter_);
  if (!iter_->valid() && !packed_column_data) {
    *result = SubDocument(ValueEntryType::kInvalid);
    return Status::OK();
  }

  auto first_row = VERIFY_RESULT(DocDbRowData::CurrentRow(
      iter_, packed_column_data, &target_subdocument_key_));

  if (first_row->key() == target_subdocument_key_) {
    ScopedDocDbRowContextWithData context(
        std::move(first_row), iter_, deadline_info_, result, ancestor_obsolescence_tracker_);
    return ProcessSubDocument(&context);
  }
  // If the currently-pointed-to key is not equal to our target, but we are still in a valid state,
  // then that key must have the target key as a prefix, meaning we are pointing to a child of our
  // target. We should therefore process the rows as if we're already in a collection, rooted at the
  // target key.
  ScopedDocDbRowContext context(
      iter_, deadline_info_, target_subdocument_key_, result, ancestor_obsolescence_tracker_);
  ScopedDocDbCollectionContext collection(&context);
  RETURN_NOT_OK(collection.SetFirstChild(std::move(first_row)));
  auto num_children = VERIFY_RESULT(ProcessChildren(&collection));
  if (num_children == 0) {
    *result = SubDocument(ValueEntryType::kTombstone);
  }
  return Status::OK();
}

SubDocumentReaderBuilder::SubDocumentReaderBuilder(
    IntentAwareIterator* iter, DeadlineInfo* deadline_info,
    std::reference_wrapper<const SchemaPackingStorage> schema_packing_storage)
    : iter_(iter), deadline_info_(deadline_info), schema_packing_storage_(schema_packing_storage) {}

SubDocumentReader SubDocumentReaderBuilder::Build(
    const KeyBytes& sub_doc_key) {
  return SubDocumentReader(sub_doc_key, iter_, deadline_info_, parent_obsolescence_tracker_);
}

Status SubDocumentReaderBuilder::InitObsolescenceInfo(
    const ObsolescenceTracker& table_obsolescence_tracker,
    const Slice& root_doc_key, const Slice& target_subdocument_key) {
  parent_obsolescence_tracker_ = table_obsolescence_tracker;
  schema_packing_ = nullptr;

  // Look at ancestors to collect ttl/write-time metadata.
  IntentAwareIteratorPrefixScope prefix_scope(root_doc_key, iter_);
  Slice prev_iter_key = target_subdocument_key.Prefix(root_doc_key.size());
  Slice temp_key = target_subdocument_key.WithoutPrefix(root_doc_key.size());
  for (;;) {
    // for each iteration of this loop, we consume another piece of the subdoc key path
    auto decode_result = VERIFY_RESULT(SubDocKey::DecodeSubkey(&temp_key));
    if (!decode_result) {
      // Stop once key_slice has consumed all subdoc keys and FindLastWriteTime has been called
      // with all but the last subdoc key
      break;
    }
    RETURN_NOT_OK(UpdateWithParentWriteInfo(prev_iter_key));
    prev_iter_key = Slice(prev_iter_key.data(), temp_key.data() - prev_iter_key.data());
  }
  DCHECK_EQ(prev_iter_key, target_subdocument_key);
  return UpdateWithParentWriteInfo(target_subdocument_key);
}

Status SubDocumentReaderBuilder::UpdateWithParentWriteInfo(
    const Slice& parent_key_without_ht) {
  Slice value;
  DocHybridTime doc_ht = parent_obsolescence_tracker_.GetHighWriteTime();
  RETURN_NOT_OK(iter_->FindLatestRecord(parent_key_without_ht, &doc_ht, &value));

  if (!iter_->valid()) {
    return Status::OK();
  }
  auto control_fields = VERIFY_RESULT(ValueControlFields::Decode(&value));

  if (value.TryConsumeByte(ValueEntryTypeAsChar::kPackedRow)) {
    schema_packing_ = &VERIFY_RESULT(schema_packing_storage_.GetPacking(&value)).get();
    packed_row_.Assign(value);
    packed_row_data_.doc_ht = doc_ht;
    packed_row_data_.control_fields = control_fields;
  }

  parent_obsolescence_tracker_ = parent_obsolescence_tracker_.Child(doc_ht);
  return Status::OK();
}

PackedColumnData SubDocumentReaderBuilder::GetPackedColumn(ColumnId column_id) {
  if (!schema_packing_) {
    return PackedColumnData();
  }

  static char null_column_type = ValueEntryTypeAsChar::kNullLow;

  if (column_id == KeyEntryValue::kLivenessColumn.GetColumnId()) {
    return PackedColumnData {
      .row = &packed_row_data_,
      .slice = Slice(&null_column_type, sizeof(null_column_type)),
    };
  }

  auto slice = schema_packing_->GetValue(column_id, packed_row_.AsSlice());
  if (!slice) {
    return PackedColumnData();
  }

  return PackedColumnData {
    .row = &packed_row_data_,
    .slice = slice->empty() ? Slice(&null_column_type, sizeof(null_column_type)) : *slice,
  };
}

}  // namespace docdb
}  // namespace yb
