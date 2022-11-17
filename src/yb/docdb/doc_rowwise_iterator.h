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

#include <string>
#include <atomic>

#include "yb/docdb/doc_reader.h"
#include "yb/rocksdb/db.h"

#include "yb/common/hybrid_time.h"
#include "yb/common/ql_scanspec.h"
#include "yb/common/read_hybrid_time.h"
#include "yb/common/schema.h"

#include "yb/docdb/doc_pgsql_scanspec.h"
#include "yb/docdb/doc_ql_scanspec.h"
#include "yb/docdb/key_bounds.h"
#include "yb/docdb/ql_rowwise_iterator_interface.h"
#include "yb/docdb/subdocument.h"
#include "yb/docdb/value.h"

#include "yb/util/status_fwd.h"
#include "yb/util/operation_counter.h"

namespace yb {
namespace docdb {

class IntentAwareIterator;
class ScanChoices;

// An SQL-mapped-to-document-DB iterator.
class DocRowwiseIterator : public YQLRowwiseIteratorIf {
 public:
  DocRowwiseIterator(const Schema &projection,
                     std::reference_wrapper<const DocReadContext> doc_read_context,
                     const TransactionOperationContext& txn_op_context,
                     const DocDB& doc_db,
                     CoarseTimePoint deadline,
                     const ReadHybridTime& read_time,
                     RWOperationCounter* pending_op_counter = nullptr);

  DocRowwiseIterator(std::unique_ptr<Schema> projection,
                     std::reference_wrapper<const DocReadContext> doc_read_context,
                     const TransactionOperationContext& txn_op_context,
                     const DocDB& doc_db,
                     CoarseTimePoint deadline,
                     const ReadHybridTime& read_time,
                     RWOperationCounter* pending_op_counter = nullptr)
      : DocRowwiseIterator(
            *projection, doc_read_context, txn_op_context, doc_db, deadline,
            read_time, pending_op_counter) {
    projection_owner_ = std::move(projection);
  }

  virtual ~DocRowwiseIterator();

  // Init scan iterator.
  Status Init(TableType table_type, const Slice& sub_doc_key = Slice());
  // Init QL read scan.
  Status Init(const QLScanSpec& spec);
  Status Init(const PgsqlScanSpec& spec);

  // This must always be called before NextRow. The implementation actually finds the
  // first row to scan, and NextRow expects the RocksDB iterator to already be properly
  // positioned.
  Result<bool> HasNext() override;

  std::string ToString() const override;

  const Schema& schema() const override {
    // Note: this is the schema only for the columns in the projection, not all columns.
    return projection_;
  }

  // Is the next row to read a row with a static column?
  bool IsNextStaticColumn() const override;

  const Slice& row_key() const {
    return row_key_;
  }

  // Check if liveness column exists. Should be called only after HasNext() has been called to
  // verify the row exists.
  bool LivenessColumnExists() const;

  // Skip the current row.
  void SkipRow() override;

  HybridTime RestartReadHt() override;

  // Returns the tuple id of the current tuple. The tuple id returned is the serialized DocKey
  // and without the cotable id.
  Result<Slice> GetTupleId() const override;

  // Seeks to the given tuple by its id. The tuple id should be the serialized DocKey and without
  // the cotable id.
  Result<bool> SeekTuple(const Slice& tuple_id) override;

  // Retrieves the next key to read after the iterator finishes for the given page.
  Status GetNextReadSubDocKey(SubDocKey* sub_doc_key) override;

  void set_debug_dump(bool value) {
    debug_dump_ = value;
  }

  static bool is_hybrid_scan_enabled();

 private:
  template <class T>
  Status DoInit(const T& spec);

  Result<bool> InitScanChoices(
      const DocQLScanSpec& doc_spec, const KeyBytes& lower_doc_key, const KeyBytes& upper_doc_key);

  Result<bool> InitScanChoices(
      const DocPgsqlScanSpec& doc_spec, const KeyBytes& lower_doc_key,
      const KeyBytes& upper_doc_key);

  // For reverse scans, moves the iterator to the first kv-pair of the previous row after having
  // constructed the current row. For forward scans nothing is necessary because GetSubDocument
  // ensures that the iterator will be positioned on the first kv-pair of the next row.
  Status AdvanceIteratorToNextDesiredRow() const;

  // Read next row into a value map using the specified projection.
  Status DoNextRow(const Schema& projection, QLTableRow* table_row) override;

  const Schema& projection_;
  // Used to maintain ownership of projection_.
  // Separate field is used since ownership could be optional.
  std::unique_ptr<Schema> projection_owner_;

  // The schema for all columns, not just the columns we're scanning.
  const DocReadContext& doc_read_context_;

  const TransactionOperationContext txn_op_context_;

  bool is_forward_scan_ = true;

  const CoarseTimePoint deadline_;

  const ReadHybridTime read_time_;

  const DocDB doc_db_;

  // A copy of the bound key of the end of the scan range (if any). We stop scan if iterator
  // reaches this point. This is exclusive bound for forward scans and inclusive bound for
  // reverse scans.
  bool has_bound_key_;
  KeyBytes bound_key_;

  std::unique_ptr<ScanChoices> scan_choices_;
  std::unique_ptr<IntentAwareIterator> db_iter_;

  // We keep the "pending operation" counter incremented for the lifetime of this iterator so that
  // RocksDB does not get destroyed while the iterator is still in use.
  ScopedRWOperation pending_op_;

  // Indicates whether we've already finished iterating.
  bool done_;

  // HasNext constructs the whole row's SubDocument.
  SubDocument row_;

  // The current row's primary key. It is set to lower bound in the beginning.
  Slice row_key_;

  // The current row's hash part of primary key.
  Slice row_hash_key_;

  // The current row's iterator key.
  KeyBytes iter_key_;

  // When HasNext constructs a row, row_ready_ is set to true.
  // When NextRow consumes the row, this variable is set to false.
  // It is initialized to false, to make sure first HasNext constructs a new row.
  bool row_ready_;

  std::vector<KeyEntryValue> projection_subkeys_;

  // Used for keeping track of errors in HasNext.
  Status has_next_status_;

  // Key for seeking a YSQL tuple. Used only when the table has a cotable id.
  boost::optional<KeyBytes> tuple_key_;

  std::unique_ptr<DocDBTableReader> doc_reader_ = nullptr;

  TableType table_type_;
  bool ignore_ttl_ = false;

  bool debug_dump_ = false;
};

}  // namespace docdb
}  // namespace yb
