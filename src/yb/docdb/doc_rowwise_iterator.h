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

#ifndef YB_DOCDB_DOC_ROWWISE_ITERATOR_H_
#define YB_DOCDB_DOC_ROWWISE_ITERATOR_H_

#include <string>
#include <atomic>

#include "yb/rocksdb/db.h"

#include "yb/common/hybrid_time.h"
#include "yb/common/ql_rowwise_iterator_interface.h"
#include "yb/common/ql_scanspec.h"
#include "yb/common/read_hybrid_time.h"
#include "yb/docdb/doc_key.h"
#include "yb/docdb/subdocument.h"
#include "yb/docdb/doc_ql_scanspec.h"
#include "yb/docdb/doc_pgsql_scanspec.h"
#include "yb/docdb/value.h"
#include "yb/docdb/deadline_info.h"
#include "yb/util/status.h"
#include "yb/util/pending_op_counter.h"

namespace yb {
namespace docdb {

class IntentAwareIterator;

// An SQL-mapped-to-document-DB iterator.
class DocRowwiseIterator : public common::YQLRowwiseIteratorIf {
 public:
  DocRowwiseIterator(const Schema &projection,
                     const Schema &schema,
                     const TransactionOperationContextOpt& txn_op_context,
                     const DocDB& doc_db,
                     CoarseTimePoint deadline,
                     const ReadHybridTime& read_time,
                     yb::util::PendingOperationCounter* pending_op_counter = nullptr);

  DocRowwiseIterator(std::unique_ptr<Schema> projection,
                     const Schema &schema,
                     const TransactionOperationContextOpt& txn_op_context,
                     const DocDB& doc_db,
                     CoarseTimePoint deadline,
                     const ReadHybridTime& read_time,
                     yb::util::PendingOperationCounter* pending_op_counter = nullptr)
      : DocRowwiseIterator(
            *projection, schema, txn_op_context, doc_db, deadline, read_time,
            pending_op_counter) {
    projection_owner_ = std::move(projection);
  }

  virtual ~DocRowwiseIterator();

  // Init scan iterator.
  CHECKED_STATUS Init();

  // Init QL read scan.
  CHECKED_STATUS Init(const common::QLScanSpec& spec);
  CHECKED_STATUS Init(const common::PgsqlScanSpec& spec);

  // This must always be called before NextRow. The implementation actually finds the
  // first row to scan, and NextRow expects the RocksDB iterator to already be properly
  // positioned.
  bool HasNext() const override;

  std::string ToString() const override;

  const Schema& schema() const override {
    // Note: this is the schema only for the columns in the projection, not all columns.
    return projection_;
  }

  // Is the next row to read a row with a static column?
  bool IsNextStaticColumn() const override;

  CHECKED_STATUS SetPagingStateIfNecessary(const QLReadRequestPB& request,
                                           const size_t num_rows_skipped,
                                           QLResponsePB* response) const override;

  CHECKED_STATUS SetPagingStateIfNecessary(const PgsqlReadRequestPB& request,
                                           PgsqlResponsePB* response) const override;

  const DocKey& row_key() const {
    return row_key_;
  }

  // Check if liveness column exists. Should be called only after HasNext() has been called to
  // verify the row exists.
  bool LivenessColumnExists() const;

  // Skip the current row.
  void SkipRow() override;

  HybridTime RestartReadHt() override;

  virtual Result<std::string> GetRowKey() const override;

  // Seek to the given key.
  virtual CHECKED_STATUS Seek(const std::string& row_key) override;

 private:

  // Retrieves the next key to read after the iterator finishes for the given page.
  CHECKED_STATUS GetNextReadSubDocKey(SubDocKey* sub_doc_key) const;

  // Get the non-key column values of a QL row.
  CHECKED_STATUS GetValues(const Schema& projection, vector<SubDocument>* values);

  // Processes a value for a column(subdoc_key) and determines if the value is valid or not based on
  // the hybrid time of subdoc_key. If valid, it is added to the values vector and is_null is set
  // to false. Otherwise, is_null is set to true.
  CHECKED_STATUS ProcessValues(const Value& value, const SubDocKey& subdoc_key,
                               vector<PrimitiveValue>* values,
                               bool *is_null) const;

  // Figures out whether the current sub_doc_key with the given top_level_value is a valid column
  // that has not expired. Sets column_found to true if this is a valid column, false otherwise.
  CHECKED_STATUS FindValidColumn(bool* column_found) const;

  // Figures out whether we have a valid column present indicating the existence of the row.
  // Sets column_found to true if a valid column is found, false otherwise.
  CHECKED_STATUS ProcessColumnsForHasNext(bool* column_found) const;

  // Verifies whether or not the column pointed to by subdoc_key is deleted by the current
  // row_delete_marker_key_.
  bool IsDeletedByRowDeletion(const SubDocKey& subdoc_key) const;

  // Given a subdoc_key pointing to a column and its associated value, determine whether or not
  // the column is valid based on TTL expiry, row level delete markers and column delete markers
  CHECKED_STATUS CheckColumnValidity(const SubDocKey& subdoc_key,
                                     const Value& value,
                                     bool* is_valid) const;

  // For reverse scans, moves the iterator to the first kv-pair of the previous row after having
  // constructed the current row. For forward scans nothing is necessary because GetSubDocument
  // ensures that the iterator will be positioned on the first kv-pair of the next row.
  CHECKED_STATUS EnsureIteratorPositionCorrect() const;

  // Read next row into a value map using the specified projection.
  CHECKED_STATUS DoNextRow(const Schema& projection, QLTableRow* table_row) override;

  // Returns true if this is a (multi)key scan (as opposed to an e.g. sequential scan).
  // It means we have a (non-empty) list of target keys that we will seek for in order (or reverse
  // order for reverse scans).
  bool IsMultiKeyScan() const {
    return range_cols_scan_options_ != nullptr;
  }

  // Utility function for (multi)key scans. Returns false if there are still target keys we need
  // to scan, and true if we are done.
  bool FinishedScanTargetsList() const {
    // We clear the options indexes array when we finished traversing all scan target options.
    return current_scan_target_idxs_.empty();
  }

  // Utility function for (multi)key scans. Updates the target scan key by incrementing the option
  // index for one column. Will handle overflow by setting current column index to 0 and
  // incrementing the previous column instead. If it overflows at first column it means we are done,
  // so it clears the scan target idxs array.
  void IncrementScanTargetAtColumn(size_t start_col) const;

  // Utility function for (multi)key scans to initialize the range portion of the current scan
  // target, scan target with the first option.
  // Only needed for scans that include the static row, otherwise Init will take care of this.
  bool InitScanTargetRangeGroupIfNeeded() const;

  // For a (multi)key scan, go to the next scan target if any. Otherwise clear the scan target idxs
  // array to mark that we are done.
  void GoToNextScanTarget() const;

  // For a (multi)key scan, go (directly) to the new target (or the one after if new_target does not
  // exist in the target list). If the new_target is larger than all scan target options it means we
  // are done so it cleares the scan target idxs array.
  void GoToScanTarget(const DocKey &new_target) const;

  const Schema& projection_;
  // Used to maintain ownership of projection_.
  // Separate field is used since ownership could be optional.
  std::unique_ptr<Schema> projection_owner_;

  // The schema for all columns, not just the columns we're scanning.
  const Schema& schema_;

  const TransactionOperationContextOpt txn_op_context_;

  bool is_forward_scan_ = true;

  const CoarseTimePoint deadline_;

  const ReadHybridTime read_time_;

  const DocDB doc_db_;

  // A copy of the bound key of the end of the scan range (if any). We stop scan if iterator
  // reaches this point. This is exclusive bound for forward scans and inclusive bound for
  // reverse scans.
  bool has_bound_key_;
  DocKey bound_key_;

  // TODO (mihnea) refactor this logic into a separate class for iterating through options.
  // For (multi)key scans (e.g. selects with 'IN' condition on the range columns) we hold the
  // options for each range column as we iteratively seek to each target key.
  // e.g. for a query "h = 1 and r1 in (2,3) and r2 in (4,5) and r3 = 6":
  //  range_cols_scan_options_   [[2, 3], [4, 5], [6]] -- value options for each column.
  //  current_scan_target_idxs_  goes from [0, 0, 0] up to [1, 1, 0] -- except when including the
  //                             static row when it starts from [0, 0, -1] instead.
  //  current_scan_target_       goes from [1][2,4,6] up to [1][3,5,6] -- is the doc key containing,
  //                             for each range column, the value (option) referenced by the
  //                             corresponding index (updated along with current_scan_target_idxs_).
  std::shared_ptr<std::vector<std::vector<PrimitiveValue>>> range_cols_scan_options_;
  mutable std::vector<std::vector<PrimitiveValue>::const_iterator> current_scan_target_idxs_;
  mutable DocKey current_scan_target_;


  std::unique_ptr<IntentAwareIterator> db_iter_;

  // We keep the "pending operation" counter incremented for the lifetime of this iterator so that
  // RocksDB does not get destroyed while the iterator is still in use.
  yb::util::ScopedPendingOperation pending_op_;

  // The mutable fields that follow are modified by HasNext, a const method.

  // Indicates whether we've already finished iterating.
  mutable bool done_;

  // HasNext constructs the whole row's SubDocument.
  mutable SubDocument row_;

  // The current row's primary key. It is set to lower bound in the beginning.
  mutable DocKey row_key_;

  // The current row's iterator key.
  mutable KeyBytes iter_key_;

  // When HasNext constructs a row, row_ready_ is set to true.
  // When NextRow consumes the row, this variable is set to false.
  // It is initialized to false, to make sure first HasNext constructs a new row.
  mutable bool row_ready_;

  mutable std::vector<PrimitiveValue> projection_subkeys_;

  // Used for keeping track of errors that happen in HasNext. Returned
  mutable Status status_;

  mutable boost::optional<DeadlineInfo> deadline_info_;
};

}  // namespace docdb
}  // namespace yb

#endif  // YB_DOCDB_DOC_ROWWISE_ITERATOR_H_
