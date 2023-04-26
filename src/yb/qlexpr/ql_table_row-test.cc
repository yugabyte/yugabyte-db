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

#include <boost/functional/hash.hpp>
#include <gtest/gtest.h>

#include "yb/qlexpr/ql_expr.h"

#include "yb/util/random_util.h"

namespace yb::qlexpr {

TEST(QLTableRowTest, Simple) {
  const ColumnIdRep column1 = kFirstColumnId.rep();
  const ColumnIdRep column2 = kFirstColumnId.rep() + 100;

  QLTableRow row;
  QLValuePB value1;
  value1.set_int32_value(42);
  QLValuePB value2;
  value2.set_string_value("test");

  row.AllocColumn(column1, value1);
  ASSERT_TRUE(row.IsColumnSpecified(column1));
  ASSERT_EQ(*row.GetColumn(column1), value1);
  ASSERT_EQ(
      Format("{ $0 => { value: int32_value: 42 ttl_seconds: 0 write_time: "
                 "kUninitializedWriteTime } }",
             kFirstColumnIdRep),
      row.ToString());

  row.AllocColumn(column2, value2);

  ASSERT_TRUE(row.IsColumnSpecified(column2));
  ASSERT_EQ(*row.GetColumn(column2), value2);
}

TEST(QLTableRowTest, Random) {
  constexpr int kRows = 100;
  constexpr int kRowIterations = 100;
  constexpr int kMutations = 10;
  constexpr int kColumns = 16;
  for (int i = kRows; i-- > 0;) {
    QLTableRow row;
    for (int j = kRowIterations; j-- > 0;) {
      row.Clear();
      std::unordered_map<ColumnId, QLValuePB, boost::hash<ColumnId>> map;
      for (int m = kMutations; m-- > 0;) {
        ColumnId column_id(kFirstColumnIdRep + RandomUniformInt(0, kColumns));
        if (map.count(column_id)) {
          continue;
        }
        QLValuePB value;
        value.set_int32_value(RandomUniformInt(0, 100));
        map.emplace(column_id, value);
        row.AllocColumn(column_id, std::move(value));

        ASSERT_EQ(row.ColumnCount(), map.size());

        for (const auto& p : map) {
          ASSERT_EQ(*row.GetColumn(p.first), p.second);
        }
      }
    }
  }
}

} // namespace yb::qlexpr
