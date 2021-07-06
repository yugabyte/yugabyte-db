// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
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
#ifndef YB_TABLET_TABLET_TEST_BASE_H
#define YB_TABLET_TABLET_TEST_BASE_H

#include <algorithm>
#include <limits>
#include <string>
#include <unordered_set>
#include <vector>

#include <boost/thread/thread.hpp>
#include <glog/logging.h>
#include <gtest/gtest.h>

#include "yb/common/partial_row.h"
#include "yb/common/ql_protocol_util.h"
#include "yb/common/ql_value.h"
#include "yb/common/row.h"
#include "yb/common/schema.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/gutil/strings/util.h"
#include "yb/gutil/walltime.h"
#include "yb/util/env.h"
#include "yb/util/memory/arena.h"
#include "yb/util/stopwatch.h"
#include "yb/util/test_graph.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"
#include "yb/tablet/local_tablet_writer.h"
#include "yb/tablet/tablet-test-util.h"
#include "yb/gutil/strings/numbers.h"

namespace yb {
namespace tablet {

// The base class takes as a template argument a "setup" class
// which can customize the schema for the tests. This way we can
// get coverage on various schemas without duplicating test code.
struct StringKeyTestSetup {
  static Schema CreateSchema() {
    return Schema({ ColumnSchema("key", STRING, false, true),
                    ColumnSchema("key_idx", INT32),
                    ColumnSchema("val", INT32) },
                  1);
  }

  void BuildRowKey(QLWriteRequestPB *req, int64_t key_idx) {
    // This is called from multiple threads, so can't move this buffer
    // to be a class member. However, it's likely to get inlined anyway
    // and loop-hosted.
    char buf[256];
    FormatKey(buf, sizeof(buf), key_idx);
    QLAddStringHashValue(req, buf);
  }

  void BuildRow(QLWriteRequestPB *req, int64_t key_idx, int32_t val = 0) {
    BuildRowKey(req, key_idx);
    QLAddInt32ColumnValue(req, kFirstColumnId + 1, key_idx);
    QLAddInt32ColumnValue(req, kFirstColumnId + 2, val);
  }

  static void FormatKey(char *buf, size_t buf_size, int64_t key_idx) {
    snprintf(buf, buf_size, "hello %" PRId64, key_idx);
  }

  string FormatDebugRow(int64_t key_idx, int32_t val, bool updated) {
    char buf[256];
    FormatKey(buf, sizeof(buf), key_idx);

    return strings::Substitute(
      "{ string_value: \"$0\" int32_value: $1 int32_value: $2 }",
      buf, key_idx, val);
  }

  // Slices can be arbitrarily large
  // but in practice tests won't overflow a uint64_t
  uint64_t GetMaxRows() const {
    return std::numeric_limits<uint64_t>::max() - 1;
  }
};

// Setup for testing composite keys
struct CompositeKeyTestSetup {
  static Schema CreateSchema() {
    return Schema({ ColumnSchema("key1", STRING, false, true),
                    ColumnSchema("key2", INT32, false, true),
                    ColumnSchema("key_idx", INT32),
                    ColumnSchema("val", INT32) },
                  2);
  }

  static void FormatKey(char *buf, size_t buf_size, int64_t key_idx) {
    snprintf(buf, buf_size, "hello %" PRId64, key_idx);
  }

  string FormatDebugRow(int64_t key_idx, int32_t val, bool updated) {
    char buf[256];
    FormatKey(buf, sizeof(buf), key_idx);
    return strings::Substitute(
      "(string key1=$0, int32 key2=$1, int32 val=$2, int32 val=$3)",
      buf, key_idx, key_idx, val);
  }

  // Slices can be arbitrarily large
  // but in practice tests won't overflow a uint64_t
  uint64_t GetMaxRows() const {
    return std::numeric_limits<uint64_t>::max() - 1;
  }
};

// Setup for testing integer keys
template<DataType Type>
struct IntKeyTestSetup {
  static Schema CreateSchema() {
    return Schema({ ColumnSchema("key", Type, false, true),
                    ColumnSchema("key_idx", INT32),
                    ColumnSchema("val", INT32) }, 1);
  }

  void BuildRowKey(QLWriteRequestPB *req, int64_t i) {
    CHECK(false) << "Unsupported type";
  }

  // builds a row key from an existing row for updates
  template<class RowType>
  void BuildRowKeyFromExistingRow(YBPartialRow *dst_row, const RowType& row) {
    CHECK(false) << "Unsupported type";
  }

  void BuildRow(QLWriteRequestPB* req, int64_t key_idx, int32_t val = 0) {
    BuildRowKey(req, key_idx);
    QLAddInt32ColumnValue(req, kFirstColumnId + 1, key_idx);
    QLAddInt32ColumnValue(req, kFirstColumnId + 2, val);
  }

  string FormatDebugRow(int64_t key_idx, int32_t val, bool updated) {
    CHECK(false) << "Unsupported type";
    return "";
  }

  uint64_t GetMaxRows() const {
    return std::numeric_limits<typename DataTypeTraits<Type>::cpp_type>::max() - 1;
  }
};

template<>
void IntKeyTestSetup<INT8>::BuildRowKey(QLWriteRequestPB *req, int64_t i) {
  QLAddInt8HashValue(req, i * (i % 2 == 0 ? -1 : 1));
}

template<>
void IntKeyTestSetup<INT16>::BuildRowKey(QLWriteRequestPB *req, int64_t i) {
  QLAddInt16HashValue(req, i * (i % 2 == 0 ? -1 : 1));
}

template<>
void IntKeyTestSetup<INT32>::BuildRowKey(QLWriteRequestPB *req, int64_t i) {
  QLAddInt32HashValue(req, i * (i % 2 == 0 ? -1 : 1));
}

template<>
void IntKeyTestSetup<INT64>::BuildRowKey(QLWriteRequestPB *req, int64_t i) {
  QLAddInt64HashValue(req, i * (i % 2 == 0 ? -1 : 1));
}

template<> template<class RowType>
void IntKeyTestSetup<INT8>::BuildRowKeyFromExistingRow(YBPartialRow *row,
                                                       const RowType& src_row) {
  CHECK_OK(row->SetInt8(0, *reinterpret_cast<const int8_t*>(src_row.cell_ptr(0))));
}

template<> template<class RowType>
void IntKeyTestSetup<INT16>::BuildRowKeyFromExistingRow(YBPartialRow *row,
                                                        const RowType& src_row) {
  CHECK_OK(row->SetInt16(0, *reinterpret_cast<const int16_t*>(src_row.cell_ptr(0))));
}
template<> template<class RowType>
void IntKeyTestSetup<INT32>::BuildRowKeyFromExistingRow(YBPartialRow *row,
                                                        const RowType& src_row) {
  CHECK_OK(row->SetInt32(0, *reinterpret_cast<const int32_t*>(src_row.cell_ptr(0))));
}

template<> template<class RowType>
void IntKeyTestSetup<INT64>::BuildRowKeyFromExistingRow(YBPartialRow *row,
                                                        const RowType& src_row) {
  CHECK_OK(row->SetInt64(0, *reinterpret_cast<const int64_t*>(src_row.cell_ptr(0))));
}

template<>
string IntKeyTestSetup<INT8>::FormatDebugRow(int64_t key_idx, int32_t val, bool updated) {
  return strings::Substitute(
    "{ int8_value: $0 int32_value: $1 int32_value: $2 }",
    (key_idx % 2 == 0) ? -key_idx : key_idx, key_idx, val);
}

template<>
string IntKeyTestSetup<INT16>::FormatDebugRow(int64_t key_idx, int32_t val, bool updated) {
  return strings::Substitute(
    "{ int16_value: $0 int32_value: $1 int32_value: $2 }",
    (key_idx % 2 == 0) ? -key_idx : key_idx, key_idx, val);
}

template<>
string IntKeyTestSetup<INT32>::FormatDebugRow(int64_t key_idx, int32_t val, bool updated) {
  return strings::Substitute(
    "{ int32_value: $0 int32_value: $1 int32_value: $2 }",
    (key_idx % 2 == 0) ? -key_idx : key_idx, key_idx, val);
}

template<>
string IntKeyTestSetup<INT64>::FormatDebugRow(int64_t key_idx, int32_t val, bool updated) {
  return strings::Substitute(
    "{ int64_value: $0 int32_value: $1 int32_value: $2 }",
    (key_idx % 2 == 0) ? -key_idx : key_idx, key_idx, val);
}

// Setup for testing nullable columns
struct NullableValueTestSetup {
  static Schema CreateSchema() {
    return Schema({ ColumnSchema("key", INT32, false, true),
                    ColumnSchema("key_idx", INT32),
                    ColumnSchema("val", INT32, true) }, 1);
  }

  void BuildRowKey(QLWriteRequestPB *req, int64_t i) {
    QLAddInt32HashValue(req, i);
  }

  // builds a row key from an existing row for updates
  template<class RowType>
  void BuildRowKeyFromExistingRow(YBPartialRow *row, const RowType& src_row) {
    CHECK_OK(row->SetInt32(0, *reinterpret_cast<const int32_t*>(src_row.cell_ptr(0))));
  }

  void BuildRow(QLWriteRequestPB *req, int64_t key_idx, int32_t val = 0) {
    BuildRowKey(req, key_idx);
    QLAddInt32ColumnValue(req, kFirstColumnId + 1, key_idx);

    if (!ShouldInsertAsNull(key_idx)) {
      QLAddInt32ColumnValue(req, kFirstColumnId + 2, val);
    }
  }

  string FormatDebugRow(int64_t key_idx, int64_t val, bool updated) {
    if (!updated && ShouldInsertAsNull(key_idx)) {
      return strings::Substitute(
      "(int32 key=$0, int32 key_idx=$1, int32 val=NULL)",
        (int32_t)key_idx, key_idx);
    }

    return strings::Substitute(
      "{ int32_value: $0 int32_value: $1 int32_value: $2 }",
      (int32_t)key_idx, key_idx, val);
  }

  static bool ShouldInsertAsNull(int64_t key_idx) {
    return (key_idx & 2) != 0;
  }

  uint64_t GetMaxRows() const {
    return std::numeric_limits<uint32_t>::max() - 1;
  }
};

// Use this with TYPED_TEST_CASE from gtest
typedef ::testing::Types<
                         StringKeyTestSetup,
                         IntKeyTestSetup<INT8>,
                         IntKeyTestSetup<INT16>,
                         IntKeyTestSetup<INT32>,
                         IntKeyTestSetup<INT64>,
                         NullableValueTestSetup
                         > TabletTestHelperTypes;

template<class TESTSETUP>
class TabletTestBase : public YBTabletTest {
 public:
  TabletTestBase() :
    YBTabletTest(TESTSETUP::CreateSchema()),
    max_rows_(setup_.GetMaxRows()),
    arena_(1024, 4*1024*1024)
  {}

  // Inserts "count" rows.
  void InsertTestRows(int64_t first_row,
                      int64_t count,
                      int32_t val,
                      TimeSeries *ts = NULL) {
    LocalTabletWriter writer(tablet().get());

    uint64_t inserted_since_last_report = 0;
    for (int64_t i = first_row; i < first_row + count; i++) {
      QLWriteRequestPB req;
      setup_.BuildRow(&req, i, val);
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

  // Inserts a single test row within a transaction.
  CHECKED_STATUS InsertTestRow(LocalTabletWriter* writer,
                               int64_t key_idx,
                               int32_t val) {
    QLWriteRequestPB req;
    req.set_type(QLWriteRequestPB::QL_STMT_INSERT);
    setup_.BuildRow(&req, key_idx, val);
    return writer->Write(&req);
  }

  CHECKED_STATUS UpdateTestRow(LocalTabletWriter* writer,
                               int64_t key_idx,
                               int32_t new_val) {
    QLWriteRequestPB req;
    req.set_type(QLWriteRequestPB::QL_STMT_UPDATE);
    setup_.BuildRowKey(&req, key_idx);
    // select the col to update (the third if there is only one key
    // or the fourth if there are two col keys).
    QLAddInt32ColumnValue(&req, kFirstColumnId + (schema_.num_key_columns() == 1 ? 2 : 3), new_val);
    return writer->Write(&req);
  }

  CHECKED_STATUS UpdateTestRowToNull(LocalTabletWriter* writer,
                                     int64_t key_idx) {
    QLWriteRequestPB req;
    req.set_type(QLWriteRequestPB::QL_STMT_UPDATE);
    setup_.BuildRowKey(&req, key_idx);
    QLAddNullColumnValue(&req, kFirstColumnId + (schema_.num_key_columns() == 1 ? 2 : 3));
    return writer->Write(&req);
  }

  CHECKED_STATUS DeleteTestRow(LocalTabletWriter* writer, int64_t key_idx) {
    QLWriteRequestPB req;
    req.set_type(QLWriteRequestPB::QL_STMT_DELETE);
    setup_.BuildRowKey(&req, key_idx);
    return writer->Write(&req);
  }

  template <class RowType>
  void VerifyRow(const RowType& row, int64_t key_idx, int32_t val) {
    ASSERT_EQ(setup_.FormatDebugRow(key_idx, val, false), schema_.DebugRow(row));
  }

  void VerifyTestRows(int64_t first_row, uint64_t expected_count) {
    auto iter = tablet()->NewRowIterator(client_schema_);
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

    QLTableRow row;
    QLValue value;
    while (ASSERT_RESULT((**iter).HasNext())) {
      ASSERT_OK_FAST((**iter).NextRow(&row));

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

  // Iterate through the full table, stringifying the resulting rows
  // into the given vector. This is only useful in tests which insert
  // a very small number of rows.
  CHECKED_STATUS IterateToStringList(vector<string> *out) {
    // TODO(dtxn) pass correct transaction ID if needed
    auto iter = this->tablet()->NewRowIterator(this->client_schema_);
    RETURN_NOT_OK(iter);
    return yb::tablet::IterateToStringList(iter->get(), out);
  }

  // because some types are small we need to
  // make sure that we don't overflow the type on inserts
  // or else we get errors because the key already exists
  uint64_t ClampRowCount(uint64_t proposal) const {
    uint64_t num_rows = min(max_rows_, proposal);
    if (num_rows < proposal) {
      LOG(WARNING) << "Clamping max rows to " << num_rows << " to prevent overflow";
    }
    return num_rows;
  }

  TESTSETUP setup_;

  const uint64_t max_rows_;

  Arena arena_;
};

} // namespace tablet
} // namespace yb

#endif  // YB_TABLET_TABLET_TEST_BASE_H"
