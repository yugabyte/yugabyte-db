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

#include "yb/docdb/doc_rowwise_iterator.h"

#include <cstdint>
#include <iterator>
#include <ostream>
#include <string>
#include <vector>

#include "yb/docdb/doc_rowwise_iterator_base.h"
#include "yb/docdb/docdb_statistics.h"
#include "yb/docdb/intent_aware_iterator.h"
#include "yb/docdb/scan_choices.h"

#include "yb/dockv/doc_key.h"
#include "yb/dockv/doc_path.h"
#include "yb/dockv/expiration.h"
#include "yb/dockv/pg_row.h"

#include "yb/qlexpr/ql_expr.h"

#include "yb/util/debug-util.h"
#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/metrics.h"
#include "yb/util/result.h"
#include "yb/util/status.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"
#include "yb/util/strongly_typed_bool.h"

using std::string;

DEFINE_RUNTIME_bool(ysql_use_flat_doc_reader, true,
    "Use DocDBTableReader optimization that relies on having at most 1 subkey for YSQL.");

DEFINE_test_flag(int32, fetch_next_delay_ms, 0, "Amount of time to delay inside FetchNext");
DEFINE_test_flag(string, fetch_next_delay_column, "", "Only delay when schema has specific column");

using namespace std::chrono_literals;

namespace yb {
namespace docdb {

DocRowwiseIterator::DocRowwiseIterator(
    const dockv::ReaderProjection& projection,
    std::reference_wrapper<const DocReadContext> doc_read_context,
    const TransactionOperationContext& txn_op_context,
    const DocDB& doc_db,
    const ReadOperationData& read_operation_data,
    std::reference_wrapper<const ScopedRWOperation> pending_op,
    const DocDBStatistics* statistics)
    : DocRowwiseIteratorBase(
          projection, doc_read_context, txn_op_context, doc_db, read_operation_data, pending_op),
      statistics_(statistics) {
}

DocRowwiseIterator::DocRowwiseIterator(
    const dockv::ReaderProjection& projection,
    std::shared_ptr<DocReadContext> doc_read_context,
    const TransactionOperationContext& txn_op_context,
    const DocDB& doc_db,
    const ReadOperationData& read_operation_data,
    ScopedRWOperation&& pending_op,
    const DocDBStatistics* statistics)
    : DocRowwiseIteratorBase(
          projection, doc_read_context, txn_op_context, doc_db, read_operation_data,
          std::move(pending_op)),
      statistics_(statistics) {
}

DocRowwiseIterator::~DocRowwiseIterator() = default;

void DocRowwiseIterator::InitIterator(
    BloomFilterMode bloom_filter_mode,
    const boost::optional<const Slice>& user_key_for_filter,
    const rocksdb::QueryId query_id,
    std::shared_ptr<rocksdb::ReadFileFilter>
        file_filter) {
  if (table_type_ == TableType::PGSQL_TABLE_TYPE) {
    ConfigureForYsql();
  }

  DCHECK(!db_iter_) << "InitIterator should be called only once.";

  db_iter_ = CreateIntentAwareIterator(
      doc_db_,
      bloom_filter_mode,
      user_key_for_filter,
      query_id,
      txn_op_context_,
      read_operation_data_,
      file_filter,
      nullptr /* iterate_upper_bound */,
      statistics_);
  InitResult();

  if (is_forward_scan_ && has_bound_key_) {
    db_iter_->SetUpperbound(bound_key_);
  }

  auto prefix = shared_key_prefix();
  if (!prefix.empty()) {
    prefix_scope_.emplace(prefix, db_iter_.get());
  }
}

void DocRowwiseIterator::ConfigureForYsql() {
  ignore_ttl_ = true;
  if (FLAGS_ysql_use_flat_doc_reader) {
    doc_mode_ = DocMode::kFlat;
  }
}

void DocRowwiseIterator::InitResult() {
  if (doc_mode_ == DocMode::kFlat) {
    row_ = std::nullopt;
  } else {
    row_.emplace();
  }
}

inline void DocRowwiseIterator::Seek(Slice key) {
  VLOG_WITH_FUNC(3) << " Seeking to " << key << "/" << dockv::DocKey::DebugSliceToString(key);
  db_iter_->Seek(key);
}

inline void DocRowwiseIterator::PrevDocKey(Slice key) {
  // TODO consider adding an operator bool to DocKey to use instead of empty() here.
  if (!key.empty()) {
    db_iter_->PrevDocKey(key);
  } else {
    db_iter_->SeekToLastDocKey();
  }
}

Status DocRowwiseIterator::AdvanceIteratorToNextDesiredRow(bool row_finished) const {
  if (scan_choices_) {
    if (!IsFetchedRowStatic()
        && !scan_choices_->CurrentTargetMatchesKey(row_key_)) {
      return scan_choices_->SeekToCurrentTarget(db_iter_.get());
    }
  }
  if (!is_forward_scan_) {
    VLOG(4) << __PRETTY_FUNCTION__ << " setting as PrevDocKey";
    db_iter_->PrevDocKey(row_key_);
  } else if (!row_finished) {
    db_iter_->SeekOutOfSubDoc(row_key_);
  }

  return Status::OK();
}

Result<bool> DocRowwiseIterator::PgFetchNext(dockv::PgTableRow* table_row) {
  if (table_row) {
    table_row->Reset();
  }
  return FetchNextImpl(table_row);
}

Result<bool> DocRowwiseIterator::DoFetchNext(
    qlexpr::QLTableRow* table_row,
    const dockv::ReaderProjection* projection,
    qlexpr::QLTableRow* static_row,
    const dockv::ReaderProjection* static_projection) {
  return FetchNextImpl(QLTableRowPair{table_row, projection, static_row, static_projection});
}

template <class TableRow>
Result<bool> DocRowwiseIterator::FetchNextImpl(TableRow table_row) {
  VLOG(4) << __PRETTY_FUNCTION__ << ", has_next_status_: " << has_next_status_ << ", done_: "
          << done_ << ", db_iter finished: " << db_iter_->IsOutOfRecords();

  // Repeated HasNext calls (without Skip/NextRow in between) should be idempotent:
  // 1. If a previous call failed we returned the same status.
  // 2. If a row is already available (row_ready_), return true directly.
  // 3. If we finished all target rows for the scan (done_), return false directly.
  RETURN_NOT_OK(has_next_status_);
  if (done_) {
    return false;
  }

  RETURN_NOT_OK(pending_op_ref_.GetAbortedStatus());

  if (PREDICT_FALSE(FLAGS_TEST_fetch_next_delay_ms > 0)) {
    const auto column_names = schema().column_names();
    if (FLAGS_TEST_fetch_next_delay_column.empty() ||
        std::find(column_names.begin(), column_names.end(), FLAGS_TEST_fetch_next_delay_column) !=
            column_names.end()) {
      YB_LOG_EVERY_N_SECS(INFO, 1)
          << "Delaying read for " << FLAGS_TEST_fetch_next_delay_ms << " ms"
          << ", schema column names: " << AsString(column_names);
      SleepFor(FLAGS_TEST_fetch_next_delay_ms * 1ms);
    }
  }

  bool first_iteration = true;
  for (;;) {
    if (db_iter_->IsOutOfRecords() || (scan_choices_ && scan_choices_->FinishedWithScanChoices())) {
      done_ = true;
      return false;
    }

    const auto key_data_result = db_iter_->FetchKey();
    if (!key_data_result.ok()) {
      VLOG(4) << __func__ << ", key data: " << key_data_result.status();
      has_next_status_ = key_data_result.status();
      return has_next_status_;
    }
    const auto& key_data = *key_data_result;

    VLOG(4) << "*fetched_key is " << dockv::SubDocKey::DebugSliceToString(key_data.key);
    if (debug_dump_) {
      LOG(INFO)
          << __func__ << ", fetched key: " << dockv::SubDocKey::DebugSliceToString(key_data.key)
          << ", " << key_data.key.ToDebugHexString();
    }

    // The iterator is positioned by the previous GetSubDocument call (which places the iterator
    // outside the previous doc_key). Ensure the iterator is pushed forward/backward indeed. We
    // check it here instead of after GetSubDocument() below because we want to avoid the extra
    // expensive FetchKey() call just to fetch and validate the key.
    auto row_key = row_key_.AsSlice();
    if (!first_iteration &&
        (is_forward_scan_ ? row_key.compare(key_data.key) >= 0
                          : row_key.compare(key_data.key) <= 0)) {
      // TODO -- could turn this check off in TPCC?
      has_next_status_ = STATUS_FORMAT(
          Corruption, "Infinite loop detected at $0, row key: $1",
          key_data.key.ToDebugString(), row_key.ToDebugString());
      LOG(DFATAL) << has_next_status_;
      return has_next_status_;
    }
    first_iteration = false;

    RETURN_NOT_OK(InitIterKey(key_data.key, dockv::IsFullRowValue(db_iter_->value())));
    row_key = row_key_.AsSlice();

    if (has_bound_key_ && is_forward_scan_ == (row_key.compare(bound_key_) >= 0)) {
      done_ = true;
      return false;
    }

    VLOG(4) << " sub_doc_key part of iter_key_ is " << dockv::DocKey::DebugSliceToString(row_key);

    bool is_static_column = IsFetchedRowStatic();
    if (scan_choices_ && !is_static_column) {
      if (!scan_choices_->CurrentTargetMatchesKey(row_key)) {
        // We must have seeked past the target key we are looking for (no result) so we can safely
        // skip all scan targets between the current target and row key (excluding row_key_ itself).
        // Update the target key and iterator and call HasNext again to try the next target.
        if (!VERIFY_RESULT(scan_choices_->SkipTargetsUpTo(row_key))) {
          // SkipTargetsUpTo returns false when it fails to decode the key.
          if (!VERIFY_RESULT(dockv::IsColocatedTableTombstoneKey(row_key))) {
            return STATUS_FORMAT(
                Corruption, "Key $0 is not table tombstone key.", row_key.ToDebugHexString());
          }
          if (is_forward_scan_) {
            db_iter_->SeekOutOfSubDoc(&row_key_);
          } else {
            db_iter_->PrevDocKey(row_key);
          }
          continue;
        }

        // We updated scan target above, if it goes past the row_key_ we will seek again, and
        // process the found key in the next loop.
        if (!scan_choices_->CurrentTargetMatchesKey(row_key)) {
          RETURN_NOT_OK(scan_choices_->SeekToCurrentTarget(db_iter_.get()));
          continue;
        }
      }
      // We found a match for the target key or a static column, so we move on to getting the
      // SubDocument.
    }

    if (doc_reader_ == nullptr) {
      doc_reader_ = std::make_unique<DocDBTableReader>(
          db_iter_.get(), read_operation_data_.deadline, &projection_, table_type_,
          schema_packing_storage());
      RETURN_NOT_OK(doc_reader_->UpdateTableTombstoneTime(
          VERIFY_RESULT(GetTableTombstoneTime(row_key))));
      if (!ignore_ttl_) {
        doc_reader_->SetTableTtl(schema());
      }
    }

    if (doc_mode_ == DocMode::kGeneric) {
      DCHECK_EQ(row_->type(), dockv::ValueEntryType::kObject);
      row_->object_container().clear();
    }

    auto doc_found_res = FetchRow(table_row);
    if (!doc_found_res.ok()) {
      has_next_status_ = doc_found_res.status();
      return has_next_status_;
    }
    const auto doc_found = *doc_found_res;
    // Use the write_time of the entire row.
    // May lose some precision by not examining write time of every column.
    IncrementKeyFoundStats(doc_found == DocReaderResult::kNotFound, key_data.write_time);

    if (scan_choices_ && !is_static_column) {
      has_next_status_ = scan_choices_->DoneWithCurrentTarget();
      RETURN_NOT_OK(has_next_status_);
    }
    has_next_status_ = AdvanceIteratorToNextDesiredRow(
        doc_found == DocReaderResult::kFoundAndFinished);
    RETURN_NOT_OK(has_next_status_);
    VLOG(4) << __func__ << ", iter: " << !db_iter_->IsOutOfRecords();

    if (doc_found != DocReaderResult::kNotFound) {
      has_next_status_ = FillRow(table_row);
      RETURN_NOT_OK(has_next_status_);
      break;
    }
  }
  return true;
}

Result<DocReaderResult> DocRowwiseIterator::FetchRow(dockv::PgTableRow* table_row) {
  CHECK_NE(doc_mode_, DocMode::kGeneric) << "Table type: " << table_type_;
  return doc_reader_->GetFlat(row_key_, table_row);
}

Result<DocReaderResult> DocRowwiseIterator::FetchRow(QLTableRowPair table_row) {
  return doc_mode_ == DocMode::kFlat ? doc_reader_->GetFlat(row_key_, table_row.table_row)
                                     : doc_reader_->Get(row_key_, &*row_);
}

Status DocRowwiseIterator::FillRow(dockv::PgTableRow* out) {
  return CopyKeyColumnsToRow(projection_, out);
}

Status DocRowwiseIterator::FillRow(QLTableRowPair out) {
  if (!out.table_row) {
    return Status::OK();
  }

  if (!out.static_row) {
    return FillRow(out.table_row, out.projection);
  }
  if (IsFetchedRowStatic()) {
    return FillRow(out.static_row, out.static_projection);
  }

  out.table_row->Clear();
  return FillRow(out.table_row, out.projection);
}

string DocRowwiseIterator::ToString() const {
  return "DocRowwiseIterator";
}

Result<HybridTime> DocRowwiseIterator::RestartReadHt() {
  return db_iter_->RestartReadHt();
}

HybridTime DocRowwiseIterator::TEST_MaxSeenHt() {
  return db_iter_->TEST_MaxSeenHt();
}

Status DocRowwiseIterator::FillRow(
    qlexpr::QLTableRow* table_row, const dockv::ReaderProjection* projection_opt) {
  VLOG(4) << __PRETTY_FUNCTION__;

  const auto& projection = projection_opt ? *projection_opt : projection_;

  if (projection.columns.empty()) {
    return Status::OK();
  }

  // Copy required key columns to table_row.
  RETURN_NOT_OK(CopyKeyColumnsToRow(projection, table_row));

  if (doc_mode_ == DocMode::kFlat) {
    return Status::OK();
  }

  DVLOG_WITH_FUNC(4) << "subdocument: " << AsString(*row_);
  const auto& schema = this->schema();
  for (const auto& column : projection.value_columns()) {
    const auto* source = row_->GetChild(column.subkey);
    auto& dest = table_row->AllocColumn(column.id);
    if (!source) {
      dest.value.Clear();
      continue;
    }
    source->ToQLValuePB(VERIFY_RESULT_REF(schema.column_by_id(column.id)).type(), &dest.value);
    dest.ttl_seconds = source->GetTtl();
    if (source->IsWriteTimeSet()) {
      dest.write_time = source->GetWriteTime();
    }
  }

  VLOG_WITH_FUNC(4) << "Returning row: " << table_row->ToString();

  return Status::OK();
}

bool DocRowwiseIterator::LivenessColumnExists() const {
  CHECK_NE(doc_mode_, DocMode::kFlat) << "Flat doc mode not supported yet";
  const auto* subdoc = row_->GetChild(dockv::KeyEntryValue::kLivenessColumn);
  return subdoc != nullptr && subdoc->value_type() != dockv::ValueEntryType::kInvalid;
}

}  // namespace docdb
}  // namespace yb
