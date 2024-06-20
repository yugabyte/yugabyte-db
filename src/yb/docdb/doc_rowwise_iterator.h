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

#include "yb/docdb/doc_pgsql_scanspec.h"
#include "yb/docdb/doc_ql_scanspec.h"
#include "yb/docdb/doc_read_context.h"
#include "yb/docdb/doc_reader.h"
#include "yb/docdb/doc_rowwise_iterator_base.h"
#include "yb/docdb/docdb_statistics.h"
#include "yb/docdb/intent_aware_iterator.h"
#include "yb/docdb/key_bounds.h"
#include "yb/docdb/ql_rowwise_iterator_interface.h"

#include "yb/dockv/subdocument.h"
#include "yb/dockv/value.h"

#include "yb/qlexpr/ql_scanspec.h"

#include "yb/rocksdb/db.h"

#include "yb/util/operation_counter.h"
#include "yb/util/status_fwd.h"

namespace yb {
namespace docdb {

class IntentAwareIterator;
class ScanChoices;

// In tests we could set doc mode to kAny to allow fetching PgTableRow for CQL table.
YB_DEFINE_ENUM(DocMode, (kGeneric)(kFlat)(kAny));

// An SQL-mapped-to-document-DB iterator.
class DocRowwiseIterator final : public DocRowwiseIteratorBase {
 public:
  DocRowwiseIterator(const dockv::ReaderProjection &projection,
                     std::reference_wrapper<const DocReadContext> doc_read_context,
                     const TransactionOperationContext& txn_op_context,
                     const DocDB& doc_db,
                     const ReadOperationData& read_operation_data,
                     std::reference_wrapper<const ScopedRWOperation> pending_op,
                     const DocDBStatistics* statistics = nullptr);

  DocRowwiseIterator(const dockv::ReaderProjection& projection,
                     std::shared_ptr<DocReadContext> doc_read_context,
                     const TransactionOperationContext& txn_op_context,
                     const DocDB& doc_db,
                     const ReadOperationData& read_operation_data,
                     ScopedRWOperation&& pending_op,
                     const DocDBStatistics* statistics = nullptr);

  ~DocRowwiseIterator() override;

  std::string ToString() const override;

  // Check if liveness column exists. Should be called only after HasNext() has been called to
  // verify the row exists.
  bool LivenessColumnExists() const;

  Result<HybridTime> RestartReadHt() override;

  void Seek(Slice key) override;

  HybridTime TEST_MaxSeenHt() override;

  // key slice should point to block of memory, that contains kHighest after the end.
  // So extended slice could be used as upperbound.
  Result<bool> PgFetchNext(dockv::PgTableRow* table_row) override;

  bool TEST_is_flat_doc() const {
    return doc_mode_ == DocMode::kFlat;
  }

  void TEST_force_allow_fetch_pg_table_row() {
    CHECK(!use_fast_backward_scan_); // Refer to doc_mode_ description.
    doc_mode_ = DocMode::kAny;
  }

 private:
  void InitIterator(
      BloomFilterMode bloom_filter_mode = BloomFilterMode::DONT_USE_BLOOM_FILTER,
      const boost::optional<const Slice>& user_key_for_filter = boost::none,
      const rocksdb::QueryId query_id = rocksdb::kDefaultQueryId,
      std::shared_ptr<rocksdb::ReadFileFilter> file_filter = nullptr) override;

  Result<bool> DoFetchNext(
      qlexpr::QLTableRow* table_row,
      const dockv::ReaderProjection* projection,
      qlexpr::QLTableRow* static_row,
      const dockv::ReaderProjection* static_projection) override;

  template <class TableRow>
  Result<bool> FetchNextImpl(TableRow table_row);

  void SeekPrevDocKey(Slice key) override;

  void ConfigureForYsql();
  void InitResult();

  // For reverse scans, moves the iterator to the first kv-pair of the previous row after having
  // constructed the current row. For forward scans nothing is necessary because GetSubDocument
  // ensures that the iterator will be positioned on the first kv-pair of the next row.
  // row_finished - true when current row was fully iterated. So we would not have to perform
  // extra Seek in case of full scan.
  // current_fetched_row_skipped is true if we skipped processing the most recently fetched row.
  Status AdvanceIteratorToNextDesiredRow(bool row_finished,
                                         bool current_fetched_row_skipped);

  // Read next row into a value map using the specified projection.
  Status FillRow(qlexpr::QLTableRow* table_row, const dockv::ReaderProjection* projection);

  struct QLTableRowPair {
    qlexpr::QLTableRow* table_row;
    const dockv::ReaderProjection* projection;
    qlexpr::QLTableRow* static_row;
    const dockv::ReaderProjection* static_projection;
  };

  Result<DocReaderResult> FetchRow(const FetchedEntry& fetched_entry, dockv::PgTableRow* table_row);
  Result<DocReaderResult> FetchRow(const FetchedEntry& fetched_entry, QLTableRowPair table_row);

  Status FillRow(QLTableRowPair table_row);
  Status FillRow(dockv::PgTableRow* table_row);

  std::unique_ptr<IntentAwareIterator> db_iter_;
  KeyBuffer prefix_buffer_;
  std::optional<IntentAwareIteratorUpperboundScope> upperbound_scope_;
  std::optional<IntentAwareIteratorLowerboundScope> lowerbound_scope_;

  // NB! Doc mode runtime change is not supported as it's value may be used in InitIterator()
  // to configure usage of fast backward scan.
  DocMode doc_mode_ = DocMode::kGeneric;

  // Points to appropriate alternative owned by result_ field.
  std::optional<dockv::SubDocument> row_;

  std::unique_ptr<DocDBTableReader> doc_reader_;

  // DocReader result returned by the previous fetch.
  DocReaderResult prev_doc_found_ = DocReaderResult::kNotFound;

  bool use_fast_backward_scan_ = false;

  const DocDBStatistics* statistics_;
};

}  // namespace docdb
}  // namespace yb
