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

#pragma once

#include <atomic>
#include <string>
#include <variant>

#include "yb/common/hybrid_time.h"
#include "yb/common/read_hybrid_time.h"
#include "yb/common/schema.h"

#include "yb/docdb/doc_reader.h"
#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/docdb/key_bounds.h"
#include "yb/docdb/ql_rowwise_iterator_interface.h"
#include "yb/docdb/read_operation_data.h"

#include "yb/dockv/dockv_fwd.h"
#include "yb/dockv/pg_key_decoder.h"
#include "yb/dockv/reader_projection.h"

#include "yb/util/status_fwd.h"
#include "yb/util/operation_counter.h"
#include "yb/docdb/doc_read_context.h"

namespace yb::docdb {

class ScanChoices;

// Base class for a SQL-mapped-to-document-DB iterator.
class DocRowwiseIteratorBase : public YQLRowwiseIteratorIf {
 public:
  DocRowwiseIteratorBase(
      const dockv::ReaderProjection& projection,
      std::reference_wrapper<const DocReadContext> doc_read_context,
      const TransactionOperationContext& txn_op_context,
      const DocDB& doc_db,
      const ReadOperationData& read_operation_data,
      std::reference_wrapper<const ScopedRWOperation> pending_op);

  DocRowwiseIteratorBase(
      const dockv::ReaderProjection& projection,
      std::reference_wrapper<const DocReadContext> doc_read_context,
      const TransactionOperationContext& txn_op_context,
      const DocDB& doc_db,
      const ReadOperationData& read_operation_data,
      ScopedRWOperation&& pending_op);

  DocRowwiseIteratorBase(
      const dockv::ReaderProjection& projection,
      std::shared_ptr<DocReadContext> doc_read_context,
      const TransactionOperationContext& txn_op_context,
      const DocDB& doc_db,
      const ReadOperationData& read_operation_data,
      ScopedRWOperation&& pending_op);

  ~DocRowwiseIteratorBase() override;

  void SetSchema(const Schema& schema);

  // Init scan iterator.
  void InitForTableType(
      TableType table_type, Slice sub_doc_key = Slice(), SkipSeek skip_seek = SkipSeek::kFalse);
  // Init QL read scan.
  Status Init(const qlexpr::YQLScanSpec& spec, SkipSeek skip_seek = SkipSeek::kFalse);

  bool IsFetchedRowStatic() const override;

  Slice row_key() const { return row_key_; }

  // Returns the tuple id of the current tuple. The tuple id returned is the serialized DocKey
  // and without the cotable id.
  Slice GetTupleId() const override;

  // Seeks to the given tuple by its id. The tuple id should be the serialized DocKey and without
  // the cotable id.
  void SeekTuple(Slice tuple_id) override;

  // Returns true if tuple was fetched, false otherwise.
  Result<bool> FetchTuple(Slice tuple_id, qlexpr::QLTableRow* row) override;

  // Retrieves the next key to read after the iterator finishes for the given page.
  Status GetNextReadSubDocKey(dockv::SubDocKey* sub_doc_key) override;

  Slice GetRowKey() const;

  void set_debug_dump(bool value) { debug_dump_ = value; }

  const Schema& schema() const {
    return *schema_;
  }

  const dockv::SchemaPackingStorage& schema_packing_storage();

 private:
  virtual void InitIterator(
      BloomFilterMode bloom_filter_mode = BloomFilterMode::DONT_USE_BLOOM_FILTER,
      const boost::optional<const Slice>& user_key_for_filter = boost::none,
      const rocksdb::QueryId query_id = rocksdb::kDefaultQueryId,
      std::shared_ptr<rocksdb::ReadFileFilter> file_filter = nullptr) = 0;

  virtual void Seek(Slice key) = 0;
  virtual void SeekPrevDocKey(Slice key) = 0;

  void CheckInitOnce();

 protected:
  // Initialize iter_key_ and update the row_key_.
  // full_row - whether is keys is related to full row value. For instance packed row.
  Status InitIterKey(Slice key, bool full_row);

  // Parse the row_key_ and copy key required key columns to row.
  Status CopyKeyColumnsToRow(const dockv::ReaderProjection& projection, qlexpr::QLTableRow* row);
  Status CopyKeyColumnsToRow(const dockv::ReaderProjection& projection, dockv::PgTableRow* row);

  template <class Row>
  Status DoCopyKeyColumnsToRow(const dockv::ReaderProjection& projection, Row* row);

  Result<DocHybridTime> GetTableTombstoneTime(Slice root_doc_key) const;

  // Increments statistics for total keys found, obsolete keys (past cutoff or no) if applicable.
  //
  // Obsolete keys include keys that are tombstoned, TTL expired, and read-time filtered.
  // If an obsolete key has a write time before the current history cutoff, records
  // a separate statistic in addition as they can be cleaned in a compaction.
  void IncrementKeyFoundStats(const bool obsolete, const EncodedDocHybridTime& write_time);

  void FinalizeKeyFoundStats();

  Slice shared_key_prefix() const;
  Slice upperbound() const;

  bool is_initialized_ = false;

 private:
  // The schema for all columns, not just the columns we're scanning.
  const std::shared_ptr<DocReadContext> doc_read_context_holder_;
  // Used to maintain ownership of doc_read_context_.
  const DocReadContext& doc_read_context_;
  const Schema* schema_;

 protected:
  const TransactionOperationContext txn_op_context_;

  bool is_forward_scan_ = true;

  const ReadOperationData read_operation_data_;

  const DocDB doc_db_;

  // A copy of the bound key of the end of the scan range (if any). We stop scan if iterator
  // reaches this point. This is exclusive bound for forward scans and inclusive bound for
  // reverse scans.
  bool has_bound_key_ = false;
  dockv::KeyBytes bound_key_;

  std::unique_ptr<ScanChoices> scan_choices_;

  // We keep the "pending operation" counter incremented for the lifetime of this iterator so that
  // RocksDB does not get destroyed while the iterator is still in use.
  ScopedRWOperation pending_op_holder_;
  const ScopedRWOperation& pending_op_ref_;

  // Indicates whether we've already finished iterating.
  bool done_ = false;

  bool fetched_row_static_ = false;

  // The current row's primary key. It is set to lower bound in the beginning.
  dockv::KeyBytes row_key_;

  const dockv::ReaderProjection& projection_;
  std::optional<dockv::PgKeyDecoder> pg_key_decoder_;

  // Key for seeking a YSQL tuple. Used only when the table has a cotable id.
  boost::optional<dockv::KeyBytes> tuple_key_;

  TableType table_type_;
  bool ignore_ttl_ = false;

  bool debug_dump_ = false;

  // History cutoff is derived from the retention policy (if present) for statistics
  // collection.
  // If no retention policy is present, an "invalid" history cutoff will be used by default
  // (i.e. all write times will be before the cutoff).
  EncodedDocHybridTime history_cutoff_;
  size_t keys_found_ = 0;
  size_t obsolete_keys_found_ = 0;
  size_t obsolete_keys_found_past_cutoff_ = 0;
};

}  // namespace yb::docdb
