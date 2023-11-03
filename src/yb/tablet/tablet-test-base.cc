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

#include "yb/tablet/tablet-test-base.h"

#include "yb/dockv/reader_projection.h"

using std::string;
using std::vector;

namespace yb {
namespace tablet {

Schema StringKeyTestSetup::CreateSchema() {
  return Schema({ ColumnSchema("key", DataType::STRING, ColumnKind::HASH),
                  ColumnSchema("key_idx", DataType::INT32),
                  ColumnSchema("val", DataType::INT32) });
}

void StringKeyTestSetup::BuildRowKey(QLWriteRequestPB *req, int64_t key_idx) {
  // This is called from multiple threads, so can't move this buffer
  // to be a class member. However, it's likely to get inlined anyway
  // and loop-hosted.
  char buf[256];
  FormatKey(buf, sizeof(buf), key_idx);
  QLAddStringHashValue(req, buf);
}

void StringKeyTestSetup::BuildRow(QLWriteRequestPB *req, int32_t key_idx, int32_t val) {
  BuildRowKey(req, key_idx);
  QLAddInt32ColumnValue(req, kFirstColumnId + 1, key_idx);
  QLAddInt32ColumnValue(req, kFirstColumnId + 2, val);
}

void StringKeyTestSetup::FormatKey(char *buf, size_t buf_size, int64_t key_idx) {
  snprintf(buf, buf_size, "hello %" PRId64, key_idx);
}

string StringKeyTestSetup::FormatDebugRow(int64_t key_idx, int32_t val, bool updated) {
  char buf[256];
  FormatKey(buf, sizeof(buf), key_idx);

  return strings::Substitute(
    "{ string_value: \"$0\" int32_value: $1 int32_value: $2 }",
    buf, key_idx, val);
}

uint32_t StringKeyTestSetup::GetMaxRows() {
  return std::numeric_limits<uint32_t>::max() - 1;
}

Schema CompositeKeyTestSetup::CreateSchema() {
  return Schema({ ColumnSchema("key1", DataType::STRING, ColumnKind::HASH),
                  ColumnSchema("key2", DataType::INT32, ColumnKind::HASH),
                  ColumnSchema("key_idx", DataType::INT32),
                  ColumnSchema("val", DataType::INT32) });
}

void CompositeKeyTestSetup::FormatKey(char *buf, size_t buf_size, int64_t key_idx) {
  snprintf(buf, buf_size, "hello %" PRId64, key_idx);
}

string CompositeKeyTestSetup::FormatDebugRow(int64_t key_idx, int32_t val, bool updated) {
  char buf[256];
  FormatKey(buf, sizeof(buf), key_idx);
  return strings::Substitute(
    "(string key1=$0, int32 key2=$1, int32 val=$2, int32 val=$3)",
    buf, key_idx, key_idx, val);
}

// Slices can be arbitrarily large
// but in practice tests won't overflow a uint64_t
uint32_t CompositeKeyTestSetup::GetMaxRows() {
  return std::numeric_limits<uint32_t>::max() - 1;
}

void TabletTestPreBase::InsertTestRows(int32_t first_row,
                                       int32_t count,
                                       int32_t val,
                                       TimeSeries *ts) {
  LocalTabletWriter writer(tablet());

  uint64_t inserted_since_last_report = 0;
  for (int32_t i = first_row; i < first_row + count; i++) {
    QLWriteRequestPB req;
    BuildRow(&req, i, val);
    CHECK_OK(writer.Write(&req));

    if ((inserted_since_last_report++ > 100) && ts) {
      ts->AddValue(static_cast<double>(inserted_since_last_report));
      inserted_since_last_report = 0;
    }
  }

  if (ts) {
    ts->AddValue(static_cast<double>(inserted_since_last_report));
  }
}

Status TabletTestPreBase::InsertTestRow(LocalTabletWriter* writer,
                                        int32_t key_idx,
                                        int32_t val) {
  QLWriteRequestPB req;
  req.set_type(QLWriteRequestPB::QL_STMT_INSERT);
  BuildRow(&req, key_idx, val);
  return writer->Write(&req);
}

Status TabletTestPreBase::UpdateTestRow(LocalTabletWriter* writer,
                                        int32_t key_idx,
                                        int32_t new_val) {
  QLWriteRequestPB req;
  req.set_type(QLWriteRequestPB::QL_STMT_UPDATE);
  BuildRowKey(&req, key_idx);
  // select the col to update (the third if there is only one key
  // or the fourth if there are two col keys).
  QLAddInt32ColumnValue(&req, kFirstColumnId + (schema_.num_key_columns() == 1 ? 2 : 3), new_val);
  return writer->Write(&req);
}

Status TabletTestPreBase::UpdateTestRowToNull(LocalTabletWriter* writer, int32_t key_idx) {
  QLWriteRequestPB req;
  req.set_type(QLWriteRequestPB::QL_STMT_UPDATE);
  BuildRowKey(&req, key_idx);
  QLAddNullColumnValue(&req, kFirstColumnId + (schema_.num_key_columns() == 1 ? 2 : 3));
  return writer->Write(&req);
}

Status TabletTestPreBase::DeleteTestRow(LocalTabletWriter* writer, int32_t key_idx) {
  QLWriteRequestPB req;
  req.set_type(QLWriteRequestPB::QL_STMT_DELETE);
  BuildRowKey(&req, key_idx);
  return writer->Write(&req);
}

void TabletTestPreBase::VerifyTestRows(int32_t first_row, int32_t expected_count) {
  dockv::ReaderProjection projection(schema_);
  auto iter = tablet()->NewRowIterator(projection);
  ASSERT_OK(iter);

  if (expected_count > INT_MAX) {
    LOG(INFO) << "Not checking rows for duplicates -- duplicates expected since "
              << "there were more than " << INT_MAX << " rows inserted.";
    return;
  }

  // Keep a bitmap of which rows have been seen from the requested
  // range.
  std::vector<bool> seen_rows;
  seen_rows.resize(expected_count);

  qlexpr::QLTableRow row;
  QLValue value;
  while (ASSERT_RESULT((**iter).FetchNext(&row))) {
    if (VLOG_IS_ON(2)) {
      VLOG(2) << "Fetched row: " << row.ToString();
    }

    ASSERT_OK(row.GetValue(schema_.column_id(1), &value));
    int32_t key_idx = value.int32_value();
    if (key_idx >= first_row && key_idx < first_row + expected_count) {
      size_t rel_idx = key_idx - first_row;
      if (seen_rows[rel_idx]) {
        FAIL() << "Saw row " << key_idx << " twice!\n"
               << "Row: " << row.ToString();
      }
      seen_rows[rel_idx] = true;
    }
  }

  // Verify that all the rows were seen.
  for (int i = 0; i < expected_count; i++) {
    ASSERT_EQ(true, seen_rows[i]) << "Never saw row: " << (i + first_row);
  }
  LOG(INFO) << "Successfully verified " << expected_count << "rows";
}

Status TabletTestPreBase::IterateToStringList(vector<string> *out) {
  // TODO(dtxn) pass correct transaction ID if needed
  dockv::ReaderProjection projection(schema_);
  auto iter = VERIFY_RESULT(tablet()->NewRowIterator(projection));
  return tablet::IterateToStringList(iter.get(), schema_, out);
}

uint32_t TabletTestPreBase::ClampRowCount(uint32_t proposal) const {
  if (max_rows_ < proposal) {
    LOG(WARNING) << "Clamping max rows to " << max_rows_ << " to prevent overflow";
    return max_rows_;
  }
  return proposal;
}

} // namespace tablet
} // namespace yb
