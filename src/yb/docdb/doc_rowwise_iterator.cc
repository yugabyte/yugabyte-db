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
#include <iterator>

#include <cstdint>
#include <ostream>
#include <string>
#include <vector>

#include "yb/common/ql_expr.h"

#include "yb/docdb/doc_key.h"
#include "yb/docdb/doc_path.h"
#include "yb/docdb/doc_rowwise_iterator_base.h"
#include "yb/docdb/docdb_statistics.h"
#include "yb/docdb/expiration.h"
#include "yb/docdb/intent_aware_iterator.h"
#include "yb/docdb/scan_choices.h"

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

DEFINE_test_flag(int32, fetch_next_delay_ms, 0,
                 "Amount of time to delay inside FetchNext");

using namespace std::chrono_literals;

namespace yb {
namespace docdb {

DocRowwiseIterator::DocRowwiseIterator(
    const Schema& projection,
    std::reference_wrapper<const DocReadContext>
        doc_read_context,
    const TransactionOperationContext& txn_op_context,
    const DocDB& doc_db,
    CoarseTimePoint deadline,
    const ReadHybridTime& read_time,
    RWOperationCounter* pending_op_counter,
    boost::optional<size_t> end_referenced_key_column_index,
    const DocDBStatistics* statistics)
    : DocRowwiseIteratorBase(
          projection, doc_read_context, txn_op_context, doc_db, deadline, read_time,
          pending_op_counter, end_referenced_key_column_index),
      statistics_(statistics) {}

DocRowwiseIterator::DocRowwiseIterator(
    std::unique_ptr<Schema> projection,
    std::reference_wrapper<const DocReadContext>
        doc_read_context,
    const TransactionOperationContext& txn_op_context,
    const DocDB& doc_db,
    CoarseTimePoint deadline,
    const ReadHybridTime& read_time,
    RWOperationCounter* pending_op_counter,
    boost::optional<size_t> end_referenced_key_column_index,
    const DocDBStatistics* statistics)
    : DocRowwiseIteratorBase(
          std::move(projection), doc_read_context, txn_op_context, doc_db, deadline, read_time,
          pending_op_counter, end_referenced_key_column_index),
      statistics_(statistics) {}

DocRowwiseIterator::DocRowwiseIterator(
    std::unique_ptr<Schema> projection,
    std::shared_ptr<DocReadContext>
        doc_read_context,
    const TransactionOperationContext& txn_op_context,
    const DocDB& doc_db,
    CoarseTimePoint deadline,
    const ReadHybridTime& read_time,
    RWOperationCounter* pending_op_counter,
    boost::optional<size_t> end_referenced_key_column_index,
    const DocDBStatistics* statistics)
    : DocRowwiseIteratorBase(
          std::move(projection), doc_read_context, txn_op_context, doc_db, deadline, read_time,
          pending_op_counter, end_referenced_key_column_index),
      statistics_(statistics) {}

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
      deadline_,
      read_time_,
      file_filter,
      nullptr /* iterate_upper_bound */,
      statistics_);
  InitResult();

  if (is_forward_scan_ && has_bound_key_) {
    db_iter_->SetUpperbound(bound_key_);
  }
}

void DocRowwiseIterator::ConfigureForYsql() {
  ignore_ttl_ = true;
  if (FLAGS_ysql_use_flat_doc_reader) {
    is_flat_doc_ = IsFlatDoc::kTrue;
  }
}

void DocRowwiseIterator::InitResult() {
  if (is_flat_doc_) {
    row_ = std::nullopt;
  } else {
    row_.emplace();
  }
}

inline void DocRowwiseIterator::Seek(const Slice& key) {
  VLOG_WITH_FUNC(3) << " Seeking to " << key;
  db_iter_->Seek(key);
}

inline void DocRowwiseIterator::PrevDocKey(const Slice& key) {
  // TODO consider adding an operator bool to DocKey to use instead of empty() here.
  if (!key.empty()) {
    db_iter_->PrevDocKey(key);
  } else {
    db_iter_->SeekToLastDocKey();
  }
}

Status DocRowwiseIterator::AdvanceIteratorToNextDesiredRow() const {
  if (scan_choices_) {
    if (!IsFetchedRowStatic()
        && !scan_choices_->CurrentTargetMatchesKey(row_key_)) {
      return scan_choices_->SeekToCurrentTarget(db_iter_.get());
    }
  }
  if (!is_forward_scan_) {
    VLOG(4) << __PRETTY_FUNCTION__ << " setting as PrevDocKey";
    db_iter_->PrevDocKey(row_key_);
  } else {
    db_iter_->SeekOutOfSubDoc(row_key_);
  }

  return Status::OK();
}

Result<bool> DocRowwiseIterator::DoFetchNext(
    QLTableRow* table_row,
    const Schema* projection,
    QLTableRow* static_row,
    const Schema* static_projection) {
  VLOG(4) << __PRETTY_FUNCTION__ << ", has_next_status_: " << has_next_status_ << ", done_: "
          << done_;

  // Repeated HasNext calls (without Skip/NextRow in between) should be idempotent:
  // 1. If a previous call failed we returned the same status.
  // 2. If a row is already available (row_ready_), return true directly.
  // 3. If we finished all target rows for the scan (done_), return false directly.
  RETURN_NOT_OK(has_next_status_);
  if (done_) {
    return false;
  }

  if (PREDICT_FALSE(FLAGS_TEST_fetch_next_delay_ms > 0)) {
    YB_LOG_EVERY_N_SECS(INFO, 1)
        << "Delaying read for " << FLAGS_TEST_fetch_next_delay_ms << " ms"
        << ", schema column names: " << AsString(doc_read_context_.schema.column_names());
    SleepFor(FLAGS_TEST_fetch_next_delay_ms * 1ms);
  }

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

    VLOG(4) << "*fetched_key is " << SubDocKey::DebugSliceToString(key_data.key);
    if (debug_dump_) {
      LOG(INFO) << __func__ << ", fetched key: " << SubDocKey::DebugSliceToString(key_data.key)
                << ", " << key_data.key.ToDebugHexString();
    }

    // The iterator is positioned by the previous GetSubDocument call (which places the iterator
    // outside the previous doc_key). Ensure the iterator is pushed forward/backward indeed. We
    // check it here instead of after GetSubDocument() below because we want to avoid the extra
    // expensive FetchKey() call just to fetch and validate the key.
    if (!iter_key_.data().empty() &&
        (is_forward_scan_ ? iter_key_.CompareTo(key_data.key) >= 0
                          : iter_key_.CompareTo(key_data.key) <= 0)) {
      // TODO -- could turn this check off in TPCC?
      has_next_status_ = STATUS_SUBSTITUTE(Corruption, "Infinite loop detected at $0",
                                           FormatSliceAsStr(key_data.key));
      return has_next_status_;
    }

    // e.g in cotable, row may point outside table bounds.
    if (!DocKeyBelongsTo(key_data.key, doc_read_context_.schema)) {
      done_ = true;
      return false;
    }

    RETURN_NOT_OK(InitIterKey(key_data.key));

    if (has_bound_key_ && is_forward_scan_ == (row_key_.compare(bound_key_) >= 0)) {
      done_ = true;
      return false;
    }

    // Prepare the DocKey to get the SubDocument. Trim the DocKey to contain just the primary key.
    Slice doc_key = row_key_;
    VLOG(4) << " sub_doc_key part of iter_key_ is " << DocKey::DebugSliceToString(doc_key);

    bool is_static_column = IsFetchedRowStatic();
    if (scan_choices_ && !is_static_column) {
      if (!scan_choices_->CurrentTargetMatchesKey(row_key_)) {
        // We must have seeked past the target key we are looking for (no result) so we can safely
        // skip all scan targets between the current target and row key (excluding row_key_ itself).
        // Update the target key and iterator and call HasNext again to try the next target.
        if (!VERIFY_RESULT(scan_choices_->SkipTargetsUpTo(row_key_))) {
          // SkipTargetsUpTo returns false when it fails to decode the key.
          if (!VERIFY_RESULT(IsColocatedTableTombstoneKey(row_key_))) {
            return STATUS_FORMAT(
                Corruption, "Key $0 is not table tombstone key.", row_key_.ToDebugHexString());
          }
          if (is_forward_scan_) {
            db_iter_->SeekOutOfSubDoc(&iter_key_);
          } else {
            db_iter_->PrevDocKey(row_key_);
          }
          continue;
        }

        // We updated scan target above, if it goes past the row_key_ we will seek again, and
        // process the found key in the next loop.
        if (!scan_choices_->CurrentTargetMatchesKey(row_key_)) {
          RETURN_NOT_OK(scan_choices_->SeekToCurrentTarget(db_iter_.get()));
          continue;
        }
      }
      // We found a match for the target key or a static column, so we move on to getting the
      // SubDocument.
    }

    if (doc_reader_ == nullptr) {
      doc_reader_ = std::make_unique<DocDBTableReader>(
          db_iter_.get(), deadline_, &reader_projection_, table_type_,
          doc_read_context_.schema_packing_storage);
      RETURN_NOT_OK(doc_reader_->UpdateTableTombstoneTime(
          VERIFY_RESULT(GetTableTombstoneTime(doc_key))));
      if (!ignore_ttl_) {
        doc_reader_->SetTableTtl(doc_read_context_.schema);
      }
    }

    if (!is_flat_doc_) {
      DCHECK(row_->type() == ValueEntryType::kObject);
      row_->object_container().clear();
    }

    auto doc_found_res =
        is_flat_doc_ ? doc_reader_->GetFlat(doc_key, table_row) : doc_reader_->Get(doc_key, &*row_);
    if (!doc_found_res.ok()) {
      has_next_status_ = doc_found_res.status();
      return has_next_status_;
    }
    const auto doc_found = *doc_found_res;
    // Use the write_time of the entire row.
    // May lose some precision by not examining write time of every column.
    IncrementKeyFoundStats(!doc_found, key_data.write_time);

    if (scan_choices_ && !is_static_column) {
      has_next_status_ = scan_choices_->DoneWithCurrentTarget(!doc_found);
      RETURN_NOT_OK(has_next_status_);
    }
    has_next_status_ = AdvanceIteratorToNextDesiredRow();
    RETURN_NOT_OK(has_next_status_);
    VLOG(4) << __func__ << ", iter: " << !db_iter_->IsOutOfRecords();

    if (doc_found) {
      if (table_row) {
        if (!static_row) {
          has_next_status_ = FillRow(table_row, projection);
        } else if (IsFetchedRowStatic()) {
          has_next_status_ = FillRow(static_row, static_projection);
        } else {
          table_row->Clear();
          has_next_status_ = FillRow(table_row, projection);
        }
        RETURN_NOT_OK(has_next_status_);
      }
      break;
    }
  }
  return true;
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
    QLTableRow* table_row, const Schema* projection_opt) {
  VLOG(4) << __PRETTY_FUNCTION__;

  // Copy required key columns to table_row.
  RETURN_NOT_OK(CopyKeyColumnsToQLTableRow(table_row));

  if (is_flat_doc_) {
    return Status::OK();
  }

  const auto& projection = projection_opt ? *projection_opt : schema();

  DVLOG_WITH_FUNC(4) << "subdocument: " << AsString(*row_);
  for (size_t i = projection.num_key_columns(); i < projection.num_columns(); i++) {
    const auto& column_id = projection.column_id(i);
    const auto ql_type = projection.column(i).type();
    const SubDocument* column_value = row_->GetChild(KeyEntryValue::MakeColumnId(column_id));
    if (column_value != nullptr) {
      QLTableColumn& column = table_row->AllocColumn(column_id);
      column_value->ToQLValuePB(ql_type, &column.value);
      column.ttl_seconds = column_value->GetTtl();
      if (column_value->IsWriteTimeSet()) {
        column.write_time = column_value->GetWriteTime();
      }
    }
  }

  VLOG_WITH_FUNC(4) << "Returning row: " << table_row->ToString();

  return Status::OK();
}

bool DocRowwiseIterator::LivenessColumnExists() const {
  CHECK(!is_flat_doc_) << "Flat doc mode not supported yet";
  const SubDocument* subdoc = row_->GetChild(KeyEntryValue::kLivenessColumn);
  return subdoc != nullptr && subdoc->value_type() != ValueEntryType::kInvalid;
}

}  // namespace docdb
}  // namespace yb
