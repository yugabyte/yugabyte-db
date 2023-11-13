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
#pragma once

#include <algorithm>
#include <limits>
#include <string>
#include <unordered_set>
#include <vector>

#include <boost/thread/thread.hpp>
#include "yb/util/logging.h"
#include <gtest/gtest.h>

#include "yb/common/partial_row.h"
#include "yb/common/ql_expr.h"
#include "yb/common/ql_protocol_util.h"
#include "yb/common/ql_value.h"

#include "yb/docdb/ql_rowwise_iterator_interface.h"

#include "yb/gutil/strings/substitute.h"
#include "yb/gutil/strings/util.h"
#include "yb/gutil/walltime.h"
#include "yb/util/env.h"
#include "yb/util/memory/arena.h"
#include "yb/util/status_log.h"
#include "yb/util/stopwatch.h"
#include "yb/util/test_graph.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"
#include "yb/tablet/local_tablet_writer.h"
#include "yb/tablet/tablet-test-util.h"
#include "yb/tablet/tablet.h"
#include "yb/gutil/strings/numbers.h"

namespace yb {
namespace tablet {

// The base class takes as a template argument a "setup" class
// which can customize the schema for the tests. This way we can
// get coverage on various schemas without duplicating test code.
struct StringKeyTestSetup {
  static Schema CreateSchema();

  void BuildRowKey(QLWriteRequestPB *req, int64_t key_idx);

  void BuildRow(QLWriteRequestPB *req, int32_t key_idx, int32_t val = 0);

  static void FormatKey(char *buf, size_t buf_size, int64_t key_idx);

  std::string FormatDebugRow(int64_t key_idx, int32_t val, bool updated);

  // Slices can be arbitrarily large
  // but in practice tests won't overflow a uint64_t
  static uint32_t GetMaxRows();
};

// Setup for testing composite keys
struct CompositeKeyTestSetup {
  static Schema CreateSchema();

  static void FormatKey(char *buf, size_t buf_size, int64_t key_idx);

  std::string FormatDebugRow(int64_t key_idx, int32_t val, bool updated);

  // Slices can be arbitrarily large
  // but in practice tests won't overflow a uint64_t
  static uint32_t GetMaxRows();
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

  void BuildRow(QLWriteRequestPB* req, int32_t key_idx, int32_t val = 0) {
    BuildRowKey(req, key_idx);
    QLAddInt32ColumnValue(req, kFirstColumnId + 1, key_idx);
    QLAddInt32ColumnValue(req, kFirstColumnId + 2, val);
  }

  std::string FormatDebugRow(int64_t key_idx, int32_t val, bool updated) {
    CHECK(false) << "Unsupported type";
    return "";
  }

  static uint32_t GetMaxRows() {
    using CppType = typename DataTypeTraits<Type>::cpp_type;
    uint64_t max = std::numeric_limits<CppType>::max();
    if (max > std::numeric_limits<uint32_t>::max()) {
      max = static_cast<CppType>(std::numeric_limits<uint32_t>::max());
    }
    return static_cast<uint32_t>(max - 1);
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
  QLAddInt32HashValue(req, narrow_cast<int32_t>(i * (i % 2 == 0 ? -1 : 1)));
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
std::string IntKeyTestSetup<INT8>::FormatDebugRow(int64_t key_idx, int32_t val, bool updated) {
  return strings::Substitute(
    "{ int8_value: $0 int32_value: $1 int32_value: $2 }",
    (key_idx % 2 == 0) ? -key_idx : key_idx, key_idx, val);
}

template<>
std::string IntKeyTestSetup<INT16>::FormatDebugRow(int64_t key_idx, int32_t val, bool updated) {
  return strings::Substitute(
    "{ int16_value: $0 int32_value: $1 int32_value: $2 }",
    (key_idx % 2 == 0) ? -key_idx : key_idx, key_idx, val);
}

template<>
std::string IntKeyTestSetup<INT32>::FormatDebugRow(int64_t key_idx, int32_t val, bool updated) {
  return strings::Substitute(
    "{ int32_value: $0 int32_value: $1 int32_value: $2 }",
    (key_idx % 2 == 0) ? -key_idx : key_idx, key_idx, val);
}

template<>
std::string IntKeyTestSetup<INT64>::FormatDebugRow(int64_t key_idx, int32_t val, bool updated) {
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

  void BuildRowKey(QLWriteRequestPB *req, int32_t i) {
    QLAddInt32HashValue(req, i);
  }

  // builds a row key from an existing row for updates
  template<class RowType>
  void BuildRowKeyFromExistingRow(YBPartialRow *row, const RowType& src_row) {
    CHECK_OK(row->SetInt32(0, *reinterpret_cast<const int32_t*>(src_row.cell_ptr(0))));
  }

  void BuildRow(QLWriteRequestPB *req, int32_t key_idx, int32_t val = 0) {
    BuildRowKey(req, key_idx);
    QLAddInt32ColumnValue(req, kFirstColumnId + 1, key_idx);

    if (!ShouldInsertAsNull(key_idx)) {
      QLAddInt32ColumnValue(req, kFirstColumnId + 2, val);
    }
  }

  std::string FormatDebugRow(int64_t key_idx, int64_t val, bool updated) {
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

  static uint32_t GetMaxRows() {
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

class TabletTestPreBase : public YBTabletTest {
 public:
  TabletTestPreBase(const Schema& schema, uint32_t max_rows)
      : YBTabletTest(schema), max_rows_(max_rows), arena_(1_KB, 4_MB) {
  }

  // Inserts "count" rows.
  void InsertTestRows(int32_t first_row,
                      int32_t count,
                      int32_t val,
                      TimeSeries *ts = nullptr);

  // Inserts a single test row within a transaction.
  Status InsertTestRow(LocalTabletWriter* writer, int32_t key_idx, int32_t val);

  Status UpdateTestRow(LocalTabletWriter* writer, int32_t key_idx, int32_t new_val);

  Status UpdateTestRowToNull(LocalTabletWriter* writer, int32_t key_idx);

  Status DeleteTestRow(LocalTabletWriter* writer, int32_t key_idx);

  void VerifyTestRows(int32_t first_row, int32_t expected_count);

  // Iterate through the full table, stringifying the resulting rows
  // into the given vector. This is only useful in tests which insert
  // a very small number of rows.
  Status IterateToStringList(std::vector<std::string> *out);

  // Because some types are small we need to
  // make sure that we don't overflow the type on inserts
  // or else we get errors because the key already exists
  uint32_t ClampRowCount(uint32_t proposal) const;

  virtual void BuildRow(QLWriteRequestPB* row, int32_t key_idx, int32_t value) = 0;
  virtual void BuildRowKey(QLWriteRequestPB* row, int32_t key_idx) = 0;

 private:
  const uint32_t max_rows_;
  Arena arena_;
};

template<class TestSetup>
class TabletTestBase : public TabletTestPreBase {
 public:
  TabletTestBase() :
    TabletTestPreBase(TestSetup::CreateSchema(), TestSetup::GetMaxRows()) {
  }

  template <class RowType>
  void VerifyRow(const RowType& row, int64_t key_idx, int32_t val) {
    ASSERT_EQ(setup_.FormatDebugRow(key_idx, val, false), schema_.DebugRow(row));
  }

  void BuildRow(QLWriteRequestPB* row, int32_t key_idx, int32_t value) override {
    setup_.BuildRow(row, key_idx, value);
  }

  void BuildRowKey(QLWriteRequestPB* row, int32_t key_idx) override {
    setup_.BuildRowKey(row, key_idx);
  }

  TestSetup setup_;
};

} // namespace tablet
} // namespace yb
