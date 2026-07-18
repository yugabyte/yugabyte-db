// Copyright (c) YugabyteDB, Inc.
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

#include "yb/docdb/ql_rocksdb_storage.h"

#include <utility>

#include "yb/common/ql_protocol.messages.h"
#include "yb/common/pgsql_protocol.messages.h"

#include "yb/dockv/doc_key.h"
#include "yb/dockv/partition.h"
#include "yb/dockv/primitive_value_util.h"

#include "yb/docdb/doc_read_context.h"
#include "yb/docdb/doc_rowwise_iterator.h"
#include "yb/docdb/doc_ql_scanspec.h"
#include "yb/docdb/docdb_statistics.h"

#include "yb/rocksdb/util/statistics.h"

#include "yb/qlexpr/ql_expr_util.h"

#include "yb/util/result.h"

namespace yb::docdb {

using dockv::DocKey;

QLRocksDBStorage::QLRocksDBStorage(
    const std::string& log_prefix, const DocDB& doc_db,
    const EncodedPartitionBounds& encoded_partition_bounds)
    : log_prefix_(log_prefix),
      doc_db_(doc_db),
      encoded_partition_bounds_(encoded_partition_bounds) {}

//--------------------------------------------------------------------------------------------------

Status QLRocksDBStorage::GetIterator(
    const QLReadRequestMsg& request,
    const dockv::ReaderProjection& projection,
    std::reference_wrapper<const DocReadContext> doc_read_context,
    const TransactionOperationContext& txn_op_context,
    const ReadOperationData& read_operation_data,
    const qlexpr::QLScanSpec& spec,
    std::reference_wrapper<const ScopedRWOperation> pending_op,
    std::unique_ptr<YQLRowwiseIteratorIf> *iter) const {
  auto doc_iter = std::make_unique<DocRowwiseIterator>(
      projection, doc_read_context, txn_op_context, doc_db_, read_operation_data, pending_op);
  RETURN_NOT_OK(doc_iter->Init(spec));
  *iter = std::move(doc_iter);
  return Status::OK();
}

Status QLRocksDBStorage::BuildYQLScanSpec(
    const QLReadRequestMsg& request, const ReadHybridTime& read_time, const Schema& schema,
    const bool include_static_columns, std::unique_ptr<qlexpr::QLScanSpec>* spec,
    std::unique_ptr<qlexpr::QLScanSpec>* static_row_spec) const {
  // Populate dockey from QL key columns.
  auto hash_code =
      request.has_hash_code() ? std::make_optional<int32_t>(request.hash_code()) : std::nullopt;
  auto max_hash_code = request.has_max_hash_code()
                           ? std::make_optional<int32_t>(request.max_hash_code())
                           : std::nullopt;

  auto arena = SharedSmallArena();
  auto hashed_components = VERIFY_RESULT(dockv::QLKeyColumnValuesToPrimitiveValues(
      request.hashed_column_values(), schema, 0, schema.num_hash_key_columns(), *arena));

  dockv::SubDocKey start_sub_doc_key;
  // Decode the start SubDocKey from the paging state and set scan start key and hybrid time.
  if (request.has_paging_state() &&
      request.paging_state().has_next_row_key() &&
      !request.paging_state().next_row_key().empty()) {

    dockv::KeyBytes start_key_bytes(request.paging_state().next_row_key());
    RETURN_NOT_OK(start_sub_doc_key.FullyDecodeFrom(start_key_bytes.AsSlice()));

    // If we start the scan with a specific primary key, the normal scan spec we return below will
    // not include the static columns if any for the start key. We need to return a separate scan
    // spec to fetch those static columns.
    const auto& start_doc_key = start_sub_doc_key.doc_key();
    if (include_static_columns && !start_doc_key.range_group().empty()) {
      const DocKey hashed_doc_key(start_doc_key.hash(), start_doc_key.hashed_group());
      static_row_spec->reset(new DocQLScanSpec(schema, hashed_doc_key,
          request.query_id(), request.is_forward_scan()));
    }
  } else if (!request.is_forward_scan() && include_static_columns) {
    dockv::KeyBytes hashed_doc_key;
    AppendHash(hash_code.value_or(0), &hashed_doc_key);
    for (Slice component : hashed_components) {
      hashed_doc_key.AppendRawBytes(component);
    }
    // Static column does not have range components. So we have 2 group ends. The first one for
    // hash components, and the seconds one for range components.
    hashed_doc_key.AppendGroupEnd();
    hashed_doc_key.AppendGroupEnd();
    *static_row_spec = std::make_unique<DocQLScanSpec>(
        schema, std::move(hashed_doc_key), request.query_id(), /* is_forward_scan = */ true);
  }

  // Construct the scan spec basing on the WHERE condition.
  *spec = std::make_unique<DocQLScanSpec>(
      schema, hash_code, max_hash_code, arena, hashed_components,
      QLConditionPBPtr(request.has_where_expr() ? &request.where_expr().condition() : nullptr),
      QLConditionPBPtr(request.has_if_expr() ? &request.if_expr().condition() : nullptr),
      request.query_id(), request.is_forward_scan(),
      request.is_forward_scan() && include_static_columns, start_sub_doc_key.doc_key());
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

Status QLRocksDBStorage::CreateIterator(
    const dockv::ReaderProjection& projection,
    std::reference_wrapper<const DocReadContext> doc_read_context,
    const TransactionOperationContext& txn_op_context,
    const ReadOperationData& read_operation_data,
    std::reference_wrapper<const ScopedRWOperation> pending_op,
    YQLRowwiseIteratorIf::UniPtr* iter) const {
  auto doc_iter = std::make_unique<DocRowwiseIterator>(
      projection, doc_read_context, txn_op_context, doc_db_, read_operation_data, pending_op);
  *iter = std::move(doc_iter);
  return Status::OK();
}

Status QLRocksDBStorage::InitIterator(DocRowwiseIterator* iter,
                                      const PgsqlReadRequestPB& request,
                                      const Schema& schema,
                                      const QLValuePB& ybctid) const {
  // Populate dockey from ybctid.
  DocKey range_doc_key(schema);
  RETURN_NOT_OK(range_doc_key.DecodeFrom(ybctid.binary_value()));
  return iter->Init(DocPgsqlScanSpec(schema, request.stmt_id(), range_doc_key));
}

Result<std::unique_ptr<YQLRowwiseIteratorIf>> QLRocksDBStorage::GetIteratorForYbctid(
    uint64 stmt_id,
    const dockv::ReaderProjection& projection,
    std::reference_wrapper<const DocReadContext> doc_read_context,
    const TransactionOperationContext& txn_op_context,
    const ReadOperationData& read_operation_data,
    const YbctidBounds& bounds,
    std::reference_wrapper<const ScopedRWOperation> pending_op,
    SkipSeek skip_seek) const {
  DocKey lower_doc_key(doc_read_context.get().schema());

  if (!bounds.first.empty()) {
    RETURN_NOT_OK(lower_doc_key.DecodeFrom(bounds.first));
  }

  DocKey upper_doc_key(doc_read_context.get().schema());

  if (!bounds.second.empty()) {
    RETURN_NOT_OK(upper_doc_key.DecodeFrom(bounds.second));
  }
  upper_doc_key.AddRangeComponent(dockv::KeyEntryValue(dockv::KeyEntryType::kHighest));
  auto doc_iter = std::make_unique<DocRowwiseIterator>(
      projection, doc_read_context, txn_op_context, doc_db_, read_operation_data, pending_op);

  static const std::vector<Slice> kEmtpySliceVec;
  static const dockv::KeyEntryValues kEmptyVec;
  RETURN_NOT_OK(doc_iter->Init(
      DocPgsqlScanSpec(
        doc_read_context.get().schema(), stmt_id,
        nullptr, /* arena */
        kEmtpySliceVec, /* hashed_components */
        kEmptyVec /* range_components */,
        nullptr /* condition */,
        std::nullopt /* hash_code */,
        std::nullopt /* max_hash_code */,
        lower_doc_key,
        true /* is_forward_scan */,
        lower_doc_key,
        upper_doc_key),
      skip_seek,
      AllowVariableBloomFilter::kTrue,
      AvoidUselessNextInsteadOfSeek::kTrue));
  return std::move(doc_iter);
}

Status QLRocksDBStorage::GetIterator(
    const PgsqlReadRequestMsg& request,
    const dockv::ReaderProjection& projection,
    std::reference_wrapper<const DocReadContext> doc_read_context,
    const TransactionOperationContext& txn_op_context,
    const ReadOperationData& read_operation_data,
    const DocKey& start_doc_key,
    std::reference_wrapper<const ScopedRWOperation> pending_op,
    YQLRowwiseIteratorIf::UniPtr* iter) const {
  const auto& schema = doc_read_context.get().schema();
  // Populate dockey from QL key columns.
  auto arena = SharedSmallArena();
  auto hashed_components = VERIFY_RESULT(qlexpr::InitKeyColumnValueSlices(
      *arena, request.partition_column_values(), schema, 0 /* start_idx */));

  auto range_components = VERIFY_RESULT(qlexpr::InitKeyColumnValues(
      request.range_column_values(), schema, schema.num_hash_key_columns()));

  auto doc_iter = std::make_unique<DocRowwiseIterator>(
      projection, doc_read_context, txn_op_context, doc_db_, read_operation_data, pending_op);

  DocKey lower_doc_key(schema);
  if (request.has_lower_bound() &&
      (schema.num_hash_key_columns() == 0 ||
       !dockv::PartitionSchema::IsValidHashPartitionKeyBound(request.lower_bound().key()))) {
      Slice lower_key_slice = request.lower_bound().key();
      RETURN_NOT_OK(lower_doc_key.DecodeFrom(
          &lower_key_slice, dockv::DocKeyPart::kWholeDocKey, dockv::AllowSpecial::kTrue));
      if (request.lower_bound().has_is_inclusive()
          && !request.lower_bound().is_inclusive()) {
          lower_doc_key.AddRangeComponent(dockv::KeyEntryValue(dockv::KeyEntryType::kHighest));
      }
  }

  DocKey upper_doc_key(schema);
  if (request.has_upper_bound() &&
      (schema.num_hash_key_columns() == 0 ||
       !dockv::PartitionSchema::IsValidHashPartitionKeyBound(request.upper_bound().key()))) {
      Slice upper_key_slice = request.upper_bound().key();
      RETURN_NOT_OK(upper_doc_key.DecodeFrom(
          &upper_key_slice, dockv::DocKeyPart::kWholeDocKey, dockv::AllowSpecial::kTrue));
      if (request.upper_bound().has_is_inclusive()
          && request.upper_bound().is_inclusive()) {
          upper_doc_key.AddRangeComponent(dockv::KeyEntryValue(dockv::KeyEntryType::kHighest));
      }
  }


  SCHECK(!request.has_where_expr(),
         InternalError,
         "WHERE clause is not yet supported in docdb::pgsql");
  RETURN_NOT_OK(doc_iter->Init(
      DocPgsqlScanSpec(
          schema,
          request.stmt_id(),
          arena,
          hashed_components,
          range_components,
          PgsqlConditionPBPtr(
                request.has_condition_expr() ? &request.condition_expr().condition() : nullptr),
          request.hash_code(),
          request.has_max_hash_code() ? std::make_optional<int32_t>(request.max_hash_code())
                                      : std::nullopt,
          start_doc_key,
          request.is_forward_scan(),
          lower_doc_key,
          upper_doc_key,
          request.prefix_length()),
      SkipSeek(request.has_index_request())));

  *iter = std::move(doc_iter);
  return Status::OK();
}

std::string QLRocksDBStorage::ToString() const {
  return doc_db_.regular->GetName();
}

namespace {

std::string IteratorPositionAsString(const rocksdb::Iterator& iter) {
  return iter.Valid() ? Format(
                            "key -> value: $0 -> $1", iter.key().ToDebugHexString(),
                            iter.value().ToDebugHexString())
                      : "<Invalid>";
}

// We use encoded doc keys or their prefixes (potentially with incremented last byte, see
// rocksdb::ShortenedIndexBuilder) as boundaries instead of full keys to avoid breaking
// data related to the same row into different sample blocks.
// Note that even if doc key was shortened and incremented by rocksdb::ShortenedIndexBuilder,
// it can't break data belonging to the same row (DocKey) into halves.
//
// Proof:
// Suppose encoded_key is a shortened doc key and there are RocksDB records belonging to the same
// DocKey (doc_key_0) that encoded_key is breaking into two halves. That means we have at least two
// RocksDB records with keys:
// key_x = [encoded_doc_key_0, x] and encoded_key_y = [encoded_doc_key_0, y].
// And encoded_key_x = [encoded_doc_key_0, x] < encoded_key < encoded_key_y = [encoded_doc_key_0, y]
// So, [encoded_doc_key_0, x] < encoded_key < [encoded_doc_key_0, y]
// => encoded_key starts with encoded_doc_key_0. And that means encoded_key contains full encoded
// DocKey (doc_key_0). This contradicts our assumption that encoded_key is a shorted doc key.
Result<Slice> ExtractDocKey(Slice encoded_key) {
  const auto doc_key_size = VERIFY_RESULT(
      dockv::DocKey::EncodedSize(encoded_key, dockv::DocKeyPart::kWholeDocKey));
  return Slice(encoded_key.data(), doc_key_size);
}

// If encoded_key_prefix contains full encoded doc key - extract encoded doc key and return it.
// Otherwise, return encoded_key itself.
Slice TryExtractDocKey(Slice encoded_key) {
  const auto doc_key_size_result =
      dockv::DocKey::EncodedSize(encoded_key, dockv::DocKeyPart::kWholeDocKey);
  if (doc_key_size_result.ok()) {
    return Slice(encoded_key.data(), doc_key_size_result.get());
  }
  return encoded_key;
}

class SampleBlocksIterator {
 public:
  SampleBlocksIterator(
      rocksdb::DataBlockAwareIndexIterator* index_iter, const Slice upperbound_key)
      : index_iter_(*index_iter), upperbound_key_(upperbound_key) {
    VLOG_IF_WITH_FUNC(3, index_iter_.Valid())
        << "Index iterator: " << IteratorPositionAsString(index_iter_)
        << " num_index_keys_processed_: " << num_index_keys_processed_;
  }

  std::string StatsToString() const {
    return Format(
        "num_index_keys_processed_: $0, num_data_blocks_accessed_: $1", num_index_keys_processed_,
        num_data_blocks_accessed_);
  }

  virtual ~SampleBlocksIterator() = default;

  virtual Result<bool> CheckedValid() const = 0;

  // Requires CheckedValid() to be true.
  virtual Status Next() = 0;

  // Only valid until successive Next() call.
  virtual Result<std::pair<Slice, Slice>> GetCurrentBlockBounds() const = 0;

 protected:
  bool HasReachedIteratorUpperbound(Slice key) {
    return !upperbound_key_.empty() && key.compare(upperbound_key_) >= 0;
  }

  rocksdb::DataBlockAwareIndexIterator& index_iter_;
  const Slice upperbound_key_;
  size_t num_index_keys_processed_ = 0;
  size_t num_data_blocks_accessed_ = 0;
};

// Uses data block boundaries for defining sample blocks.
// Previous sample block upperbound is used as lower bound for the next sample block.
// When we have multiple SST files, data block boundaries can have intersection with boundaries
// of the previous data block and sample block will only contain part of the data block while
// previous part of the data block will be covered by previous sample block.
class SplitIntersectingSampleBlocksIterator : public SampleBlocksIterator {
 public:
  SplitIntersectingSampleBlocksIterator(
      rocksdb::DataBlockAwareIndexIterator* index_iter, const Slice upperbound_key)
      : SampleBlocksIterator(index_iter, upperbound_key) {
    status_ = UpdateSampleBlockBounds();
  }

  Result<bool> CheckedValid() const override {
    RETURN_NOT_OK(status_);
    return !done_;
  }

  Status Next() override {
    SCHECK(VERIFY_RESULT(CheckedValid()), IllegalState, "Iterator is required to be valid.");

    if (current_sample_block_upper_bound_.empty()) {
      done_ = true;
      return Status::OK();
    }

    MoveUpperBoundToLower();

    do {
      ++num_index_keys_processed_;
      index_iter_.Next();
      status_ = index_iter_.status();
      RETURN_NOT_OK(status_);
      status_ = UpdateSampleBlockBounds();
      RETURN_NOT_OK(status_);
      // Skipping duplicate doc keys if any.
    } while (!current_sample_block_upper_bound_.empty() &&
             current_sample_block_upper_bound_.compare(current_sample_block_lower_bound_) <= 0);

    VLOG_IF_WITH_FUNC(4, index_iter_.Valid())
        << "Index iterator: " << IteratorPositionAsString(index_iter_)
        << " data block bounds: " << AsDebugHexString(index_iter_.GetCurrentDataBlockBounds())
        << " num_index_keys_processed_: " << num_index_keys_processed_;
    return Status::OK();
  }

  Result<std::pair<Slice, Slice>> GetCurrentBlockBounds() const override {
    SCHECK(VERIFY_RESULT(CheckedValid()), IllegalState, "Iterator is required to be valid.");
    return std::pair<Slice, Slice>(
        current_sample_block_lower_bound_, current_sample_block_upper_bound_);
  }

 private:
    inline void MoveUpperBoundToLower() {
      // Same as current_sample_block_lower_bound_ = current_sample_block_upper_bound_, but more
      // effective due to move. It is not safe to access data from current_sample_block_upper_bound_
      // after move because it referred to moved data but still safe to read the size.
      current_sample_block_lower_bound_ = std::move(current_data_block_bounds_.second);
      current_sample_block_lower_bound_.resize(current_sample_block_upper_bound_.size());
    }

  Status UpdateSampleBlockBounds() {
    if (!index_iter_.Valid()) {
      RETURN_NOT_OK(index_iter_.status());
      // Last sample block.
      current_sample_block_upper_bound_.Clear();
      return Status::OK();
    }
    current_data_block_bounds_ = VERIFY_RESULT(index_iter_.GetCurrentDataBlockBounds());
    ++num_data_blocks_accessed_;
    VLOG_WITH_FUNC(3) << "Data block bounds: " << AsDebugHexString(current_data_block_bounds_);

    current_sample_block_upper_bound_ =
        VERIFY_RESULT(ExtractDocKey(current_data_block_bounds_.second));
    if (HasReachedIteratorUpperbound(current_sample_block_upper_bound_)) {
      // Last sample block.
      current_sample_block_upper_bound_.Clear();
    }
    return Status::OK();
  }

  Status status_;
  std::pair<std::string, std::string> current_data_block_bounds_;
  std::string current_sample_block_lower_bound_;
  Slice current_sample_block_upper_bound_;
  bool done_ = false;
};

// Uses DocKey prefixes of index keys as boundaries of sample blocks, avoids data block access for
// blocks sampling phase.
class SplitIntersectingSampleBlocksIteratorV3 : public SampleBlocksIterator {
 public:
  SplitIntersectingSampleBlocksIteratorV3(
      rocksdb::DataBlockAwareIndexIterator* index_iter, const Slice upperbound_key)
      : SampleBlocksIterator(index_iter, upperbound_key) {
    UpdateSampleBlockUpperbound();
  }

  Result<bool> CheckedValid() const override {
    RETURN_NOT_OK(index_iter_.status());
    return !done_;
  }

  Status Next() override {
    SCHECK(VERIFY_RESULT(CheckedValid()), IllegalState, "Iterator is required to be valid.");

    if (current_sample_block_upper_bound_.empty()) {
      done_ = true;
      return Status::OK();
    }

    current_sample_block_lower_bound_ = std::move(current_sample_block_upper_bound_);

    do {
      ++num_index_keys_processed_;
      index_iter_.Next();
      RETURN_NOT_OK(index_iter_.status());
      UpdateSampleBlockUpperbound();
      // Skipping duplicate doc keys if any.
    } while (!current_sample_block_upper_bound_.empty() &&
             current_sample_block_lower_bound_ == current_sample_block_upper_bound_);

    VLOG_IF_WITH_FUNC(4, index_iter_.Valid())
        << "Index iterator: " << IteratorPositionAsString(index_iter_)
        << " data block bounds: " << AsDebugHexString(index_iter_.GetCurrentDataBlockBounds())
        << " num_index_keys_processed_: " << num_index_keys_processed_;
    return Status::OK();
  }

  Result<std::pair<Slice, Slice>> GetCurrentBlockBounds() const override {
    SCHECK(VERIFY_RESULT(CheckedValid()), IllegalState, "Iterator is required to be valid.");
    VLOG_WITH_FUNC(3) << "Index iterator: " << IteratorPositionAsString(index_iter_)
                      << " num_index_keys_processed_: " << num_index_keys_processed_;
    return std::pair<Slice, Slice>(
        current_sample_block_lower_bound_.AsSlice(), current_sample_block_upper_bound_.AsSlice());
  }

 private:
  void UpdateSampleBlockUpperbound() {
    const auto& entry = index_iter_.Entry();
    if (!entry.Valid()) {
      // Last sample block.
      current_sample_block_upper_bound_.Clear();
      return;
    }
    // If entry.key contains full encoded doc key - extract it.
    // Otherwise, entry.key is a shortened encoded doc key which could be used as a boundary,
    // see comments for ExtractDocKey, TryExtractDocKey and
    // YQLRowwiseIteratorIf::SeekToDocKeyPrefix.
    current_sample_block_upper_bound_ = TryExtractDocKey(entry.key);
    if (HasReachedIteratorUpperbound(current_sample_block_upper_bound_.AsSlice())) {
      // Last sample block.
      current_sample_block_upper_bound_.Clear();
    }
  }

  KeyBuffer current_sample_block_lower_bound_;
  KeyBuffer current_sample_block_upper_bound_;
  bool done_ = false;
};

class CombineIntersectingSampleBlocksIterator : public SampleBlocksIterator {
 public:
  CombineIntersectingSampleBlocksIterator(
      rocksdb::DataBlockAwareIndexIterator* index_iter, const Slice upperbound_key)
      : SampleBlocksIterator(index_iter, upperbound_key) {
    if (index_iter_.Valid()) {
      status_ = UpdateSampleBlockBounds();
    } else {
      status_ = index_iter_.status();
      done_ = true;
    }
  }

  Result<bool> CheckedValid() const override {
    RETURN_NOT_OK(status_);
    return !done_;
  }

  Status Next() override {
    RETURN_NOT_OK(status_);
    SCHECK(VERIFY_RESULT(CheckedValid()), IllegalState, "Iterator is required to be valid.");

    if (current_sample_block_upper_bound_.empty()) {
      done_ = true;
      return Status::OK();
    }

    MoveUpperBoundToPrev();

    bool skipped_index_key = false;
    for (;;) {
      ++num_index_keys_processed_;
      index_iter_.Next();
      VLOG_IF_WITH_FUNC(3, index_iter_.Valid())
          << "Index iterator: " << IteratorPositionAsString(index_iter_)
          << " num_index_keys_processed_: " << num_index_keys_processed_;

      if (!index_iter_.Valid()) {
        status_ = index_iter_.status();
        RETURN_NOT_OK(status_);
        if (skipped_index_key) {
          // We've reached end of tablet but skipped last index key. Form a sample block from
          // prev_sample_block_upper_bound_ to the end of the tablet to avoid skipping last block
          // rows.
          current_data_block_bounds_.second.clear();
          current_sample_block_upper_bound_.Clear();
        } else {
          done_ = true;
        }
        break;
      }
      status_ = UpdateSampleBlockBounds();
      RETURN_NOT_OK(status_);

      const auto data_block_start_key =
          VERIFY_RESULT(ExtractDocKey(current_data_block_bounds_.first));
      if (data_block_start_key.compare(prev_sample_block_upper_bound_) <= 0) {
        // Skip data blocks that start before or at previous sample block lower bound.
        skipped_index_key = true;
        continue;
      }
      break;
    }

    return Status::OK();
  }

  Result<std::pair<Slice, Slice>> GetCurrentBlockBounds() const override {
    SCHECK(VERIFY_RESULT(CheckedValid()), IllegalState, "Iterator is required to be valid.");
    return std::pair<Slice, Slice>(
        prev_sample_block_upper_bound_, current_sample_block_upper_bound_);
  }

 private:
  inline void MoveUpperBoundToPrev() {
    // Same as prev_sample_block_upper_bound_ = current_sample_block_upper_bound_, but more
    // effective due to move. It is not safe to access data from current_sample_block_upper_bound_
    // after move because it referred to moved data but still safe to read the size.
    prev_sample_block_upper_bound_ = std::move(current_data_block_bounds_.second);
    prev_sample_block_upper_bound_.resize(current_sample_block_upper_bound_.size());
  }

  Status UpdateSampleBlockBounds() {
    current_data_block_bounds_ = VERIFY_RESULT(index_iter_.GetCurrentDataBlockBounds());
    ++num_data_blocks_accessed_;
    VLOG_WITH_FUNC(3) << "Data block bounds: " << AsDebugHexString(current_data_block_bounds_);
    current_sample_block_upper_bound_ =
        VERIFY_RESULT(ExtractDocKey(current_data_block_bounds_.second));
    if (HasReachedIteratorUpperbound(current_sample_block_upper_bound_)) {
      // Last sample block.
      current_sample_block_upper_bound_.Clear();
    }
    return Status::OK();
  }

  Status status_;
  std::pair<std::string, std::string> current_data_block_bounds_;
  std::string prev_sample_block_upper_bound_;
  Slice current_sample_block_upper_bound_;
  bool done_ = false;
};

Result<std::unique_ptr<SampleBlocksIterator>> CreateSampleBlocksIterator(
    DocDbBlocksSamplingMethod blocks_sampling_method,
    rocksdb::DataBlockAwareIndexIterator* index_iterator, Slice table_upperbound_key) {
  switch (blocks_sampling_method) {
    case DocDbBlocksSamplingMethod::SPLIT_INTERSECTING_BLOCKS:
      return std::make_unique<SplitIntersectingSampleBlocksIterator>(
          index_iterator, table_upperbound_key);

    case DocDbBlocksSamplingMethod::SPLIT_INTERSECTING_BLOCKS_V3:
      return std::make_unique<SplitIntersectingSampleBlocksIteratorV3>(
          index_iterator, table_upperbound_key);

    case DocDbBlocksSamplingMethod::COMBINE_INTERSECTING_BLOCKS:
      return std::make_unique<CombineIntersectingSampleBlocksIterator>(
          index_iterator, table_upperbound_key);
  }
  return STATUS_FORMAT(
      InvalidArgument, "Invalid docdb_blocks_sampling_method specified: $0",
      blocks_sampling_method);
}

void PutSampleBlockToReservoir(const std::pair<Slice, Slice> sample_block, size_t index,
    QLRocksDBStorage::SampleBlocksReservoir* reservoir) {
  VLOG_WITH_FUNC(3) << "Putting sample block to reservoir at index #" << index
                    << ", block bounds: " << AsDebugHexString(sample_block);
  auto adjusted_sample_block_bounds = std::make_pair(
      KeyBuffer(sample_block.first), KeyBuffer(sample_block.second));
  (*reservoir)[index] = std::move(adjusted_sample_block_bounds);
}

} // namespace

Result<YQLStorageIf::SampleBlocksReservoir> QLRocksDBStorage::GetSampleBlocks(
    std::reference_wrapper<const DocReadContext> doc_read_context,
    DocDbBlocksSamplingMethod blocks_sampling_method,
    size_t num_blocks_for_sample, BlocksSamplingState* state) const {

  struct ScopedStats {
    ScopedStats(const DocDB& doc_db_, int vlog_level_) : doc_db(doc_db_), vlog_level(vlog_level_) {}

    ~ScopedStats() {
      if (VLOG_IS_ON(vlog_level)) {
        std::stringstream ss;
        docdb_stats.Dump(&ss);
        VLOG(vlog_level) << "DocDB stats:\n" << ss.str();
      }
      docdb_stats.MergeAndClear(
          doc_db.regular->GetOptions().statistics.get(),
          doc_db.intents->GetOptions().statistics.get());
    }

    const DocDB& doc_db;
    const int vlog_level;
    DocDBStatistics docdb_stats;
  };

  std::unique_ptr<ScopedStats> scoped_stats;
  if (VLOG_IS_ON(2)) {
    // Don't print stats by default to avoid log spew in default operation mode.
    scoped_stats = std::make_unique<ScopedStats>(doc_db_, /* vlog_level = */ 2);
  }

  LOG_WITH_PREFIX_AND_FUNC(INFO) << "num_blocks_for_sample: " << num_blocks_for_sample
                                 << " state: " << state->ToString();

  const auto is_colocated = doc_read_context.get().schema().is_colocated();

  Slice partition_lower_bound_key;
  Slice partition_upper_bound_key;

  {
    const Slice table_key_prefix = doc_read_context.get().table_key_prefix();
    const Slice table_upperbound_key = doc_read_context.get().upperbound();
    VLOG_WITH_PREFIX_AND_FUNC(2) << "table_key_prefix: " << table_key_prefix.ToDebugHexString()
                                 << " table_upperbound_key: "
                                 << table_upperbound_key.ToDebugHexString();

    if (is_colocated) {
      partition_lower_bound_key = table_key_prefix;
      partition_upper_bound_key = table_upperbound_key;
    } else {
      partition_lower_bound_key = encoded_partition_bounds_.start_key.AsSlice();
      partition_upper_bound_key = encoded_partition_bounds_.end_key.AsSlice();
    }
  }

  VLOG_WITH_PREFIX_AND_FUNC(2) << "partition_lower_bound_key: "
                               << partition_lower_bound_key.ToDebugHexString()
                               << " partition_upper_bound_key: "
                               << partition_upper_bound_key.ToDebugHexString()
                               << " is_colocated: " << is_colocated;

  rocksdb::ReadOptions read_options;
  if (scoped_stats) {
    read_options.statistics = scoped_stats->docdb_stats.RegularDBStatistics();
  }

  // An index block contains one entry per data block, where the key is a string >= last key in that
  // data block and < the first key in the successive data block. The value is the BlockHandle
  // (file offset and length) for the data block.
  // We are skipping last index iterator entry because we are replacing it with empty key which
  // will be handled as a table upper bound.
  auto index_iter =
      doc_db_.regular->NewDataBlockAwareIndexIterator(read_options, rocksdb::SkipLastEntry::kTrue);

  Slice table_rows_start = partition_lower_bound_key.empty()
                               ? Slice(&dockv::kMinRegularDbTableRowFirstByte, 1)
                               : partition_lower_bound_key;
  index_iter->Seek(table_rows_start);
  if (index_iter->Valid() && index_iter->key() == table_rows_start) {
    // Index iterator points to data block with keys <= table_rows_start, skip it.
    index_iter->Next();
  }
  VLOG_WITH_PREFIX_AND_FUNC(2) << "index_iter: " << index_iter->KeyDebugHexString();

  SampleBlocksReservoir blocks_reservoir;
  blocks_reservoir.reserve(num_blocks_for_sample);
  blocks_reservoir.insert(blocks_reservoir.begin(), num_blocks_for_sample, {});

  auto sample_block_iter = VERIFY_RESULT(CreateSampleBlocksIterator(
      blocks_sampling_method, index_iter.get(), partition_upper_bound_key));

  Status status;
  for (; VERIFY_RESULT(sample_block_iter->CheckedValid()); status = sample_block_iter->Next()) {
    RETURN_NOT_OK(status);
    auto block_bounds = VERIFY_RESULT(sample_block_iter->GetCurrentBlockBounds());
    VLOG_WITH_PREFIX_AND_FUNC(4) << "Got sample block bounds: " << AsDebugHexString(block_bounds)
                                 << " state: " << state->ToString();
    if (block_bounds.first.empty()) {
      block_bounds.first = partition_lower_bound_key;
    }
    if (block_bounds.second.empty()) {
      block_bounds.second = partition_upper_bound_key;
    }
    if (block_bounds.first == block_bounds.second && !block_bounds.first.empty()) {
      // Skip empty sample blocks. (The one with both boundaries not set is treated as the whole
      // partition, so it isn't empty).
      continue;
    }
    if (state->num_blocks_collected < num_blocks_for_sample) {
      PutSampleBlockToReservoir(block_bounds, state->num_blocks_collected, &blocks_reservoir);
      ++state->num_blocks_collected;
    } else if (RandomActWithProbability(
                   1.0 * num_blocks_for_sample / (state->num_blocks_processed + 1))) {
      const auto replace_idx = RandomUniformInt<size_t>(0, num_blocks_for_sample - 1);
      PutSampleBlockToReservoir(block_bounds, replace_idx, &blocks_reservoir);
    }
    ++state->num_blocks_processed;
  }

  LOG_WITH_PREFIX_AND_FUNC(INFO) << "state: " << state->ToString();

  return std::move(blocks_reservoir);
}

}  // namespace yb::docdb
