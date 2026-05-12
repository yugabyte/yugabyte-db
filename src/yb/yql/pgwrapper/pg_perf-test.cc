// Copyright (c) YugabyteDB, Inc.
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

#include "yb/tserver/mini_tablet_server.h"

#include "yb/util/range.h"

#include "yb/yql/pgwrapper/pg_mini_test_base.h"
#include "yb/yql/pgwrapper/pg_test_utils.h"

METRIC_DECLARE_histogram(handler_latency_yb_tserver_TabletServerService_Write);

namespace yb::pgwrapper {

class PgPerfTest : public PgMiniTestBase {
 protected:
};

YB_DEFINE_ENUM(ColumnType, (kInt)(kText));

struct ColumnDescription {
  std::string name;
  ColumnType type;
};

using ColumnDescriptions = std::vector<ColumnDescription>;

class QueryHelper {
 public:
  explicit QueryHelper(std::string_view table_name, int num_keys, ColumnDescriptions&& columns)
      : table_name_(table_name), num_keys_(num_keys), columns_(std::move(columns)) {}

  std::string CreateTable() const {
    std::string result = Format("CREATE TABLE $0 (", table_name_);
    std::string suffix;
    int idx = 0;
    for (const auto& [column_name, column_type] : columns_) {
      result += Format("$0 $1, ", column_name, AsString(column_type).substr(1));
      if (idx++ < num_keys_) {
        if (suffix.empty()) {
          suffix += "PRIMARY KEY (";
        } else {
          suffix += ", ";
        }
        suffix += column_name;
      }
    }
    suffix += "))";
    result += suffix;

    return result;
  }

  std::string InsertNRows(int num_rows, int start_row = 0) const {
    std::string result = Format("INSERT INTO $0 SELECT ", table_name_);
    int idx = 0;
    for (const auto& [column_name, column_type] : columns_) {
      if (idx++) {
        result += ", ";
      }
      switch (column_type) {
        case ColumnType::kInt:
          result += "i";
          break;
        case ColumnType::kText:
          result += "ENCODE(GEN_RANDOM_BYTES(1000), 'base64')";
          break;
      }
    }
    result += Format(" FROM generate_series($0, $1) as i", start_row, start_row + num_rows - 1);
    return result;
  }
 private:
  std::string table_name_;
  int num_keys_;
  ColumnDescriptions columns_;
};

TEST_F(PgPerfTest, InsertStrings) {
  constexpr int kNumValues = 5;
  constexpr int kNumRows = RegularBuildVsDebugVsSanitizers(1000, 10, 1) * 100;

  auto conn = ASSERT_RESULT(Connect());

  ASSERT_OK(conn.Execute("CREATE EXTENSION IF NOT EXISTS pgcrypto"));

  ColumnDescriptions columns = {{"pk", ColumnType::kInt}};
  for (int i : Range(1, kNumValues)) {
    columns.push_back({Format("c$0", i), ColumnType::kText});
  }
  QueryHelper query_helper("test", 1, std::move(columns));
  ASSERT_OK(conn.Execute(query_helper.CreateTable()));
  std::vector<std::pair<const HdrHistogram*, int64_t>> histograms;
  for (const auto& tserver : cluster_->mini_tablet_servers()) {
    auto histogram = tserver->metric_entity().FindOrCreateMetric<Histogram>(
        &METRIC_handler_latency_yb_tserver_TabletServerService_Write)->underlying();
    histograms.emplace_back(histogram, histogram->TotalSum());
  }
  auto start = MonoTime::Now();
  ASSERT_OK(conn.Execute(query_helper.InsertNRows(kNumRows)));
  auto finish = MonoTime::Now();
  auto tserver_total = MonoDelta::kZero;
  for (const auto& [histogram, initial] : histograms) {
    auto current = MonoDelta::FromMicroseconds(histogram->TotalSum() - initial);
    LOG(INFO) << "TServer time: " << current;
    tserver_total += current;
  }
  LOG(INFO) << "Passed: " << finish - start << ", tserver total: " << tserver_total;
}

} // namespace yb::pgwrapper
