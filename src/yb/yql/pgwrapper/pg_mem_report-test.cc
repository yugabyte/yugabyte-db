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

#include "yb/util/test_macros.h"
#include "yb/util/tostring.h"

#include "yb/yql/pgwrapper/pg_mem_report.h"
#include "yb/yql/pgwrapper/pg_mini_test_base.h"

DECLARE_bool(TEST_ysql_pg_mem_report);
DECLARE_int64(TEST_ysql_pg_mem_backend_peak_kb);

using namespace std::literals;

namespace yb::pgwrapper {

TEST(PgMemReportParseTest, Record) {
  auto fields = ASSERT_RESULT(ParsePgMemLine(
      "I1124 01:02:03 [1234] LOG:  YB_PG_MEM_RECORD pm=10 pid=11 type=client_backend "
      "peak_kb=2048 over=peak db=\"yugabyte\" app=\"\" stmt=\"SELECT 'a \\\"b\\\" c'\\nFROM t\""));
  ASSERT_EQ(fields["pm"], "10");
  ASSERT_EQ(fields["pid"], "11");
  ASSERT_EQ(fields["type"], "client_backend");
  ASSERT_EQ(fields["peak_kb"], "2048");
  ASSERT_EQ(fields["over"], "peak");
  ASSERT_EQ(fields["db"], "yugabyte");
  ASSERT_EQ(fields["app"], "");
  ASSERT_EQ(fields["stmt"], "SELECT 'a \"b\" c'\nFROM t");
}

TEST(PgMemReportParseTest, Errors) {
  ASSERT_NOK(ParsePgMemLine("no marker"));
  ASSERT_NOK(ParsePgMemLine("YB_PG_MEM_RECORD pid"));
  ASSERT_NOK(ParsePgMemLine("YB_PG_MEM_RECORD stmt=\"unterminated"));
}

class PgMemReportTest : public PgMiniTestBase {
 protected:
  size_t NumTabletServers() override {
    return 1;
  }

  // Stops postgres and waits until all of its output has been collected.
  Result<PgMemReport> StopPostgresAndCollect() {
    pg_supervisor_->Stop();
    auto& collector = PgMemReportCollector::Get();
    RETURN_NOT_OK(collector.WaitForSourcesClosed(30s * kTimeMultiplier));
    LOG(INFO) << "Collected lines:\n" << yb::ToString(collector.Lines());
    return collector.Report();
  }
};

TEST_F(PgMemReportTest, RecordsEveryProcess) {
  {
    auto conn = ASSERT_RESULT(Connect());
    ASSERT_OK(conn.Execute("CREATE TABLE t (k INT PRIMARY KEY, v TEXT)"));
    ASSERT_OK(conn.Execute(
        "INSERT INTO t SELECT i, repeat('x', 1000) FROM generate_series(1, 1000) i"));
  }
  auto report = ASSERT_RESULT(StopPostgresAndCollect());
  auto& collector = PgMemReportCollector::Get();
  ASSERT_FALSE(report.enforce);
  ASSERT_TRUE(report.violations.empty()) << report.ToString();
  ASSERT_TRUE(report.warnings.empty()) << report.ToString();

  bool found_backend = false;
  bool found_postmaster = false;
  for (const auto& line : collector.Lines()) {
    if (line.find("YB_PG_MEM_RECORD") == std::string::npos) {
      continue;
    }
    auto fields = ASSERT_RESULT(ParsePgMemLine(line));
    if (fields["type"] == "postmaster") {
      found_postmaster = true;
    }
    if (fields["type"] == "client_backend" && fields["db"] == "yugabyte") {
      found_backend = true;
      ASSERT_GT(std::stoll(fields["peak_kb"]), 0) << line;
      ASSERT_GT(std::stoll(fields["idle_anon_kb"]), 0) << line;
      ASSERT_GT(std::stoll(fields["idle_samples"]), 0) << line;
    }
  }
  ASSERT_TRUE(found_backend);
  ASSERT_TRUE(found_postmaster);
}

class PgMemReportOverBudgetTest : public PgMemReportTest {
 protected:
  void SetUp() override {
    // Every backend's peak RSS is above 1 kB.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_ysql_pg_mem_backend_peak_kb) = 1;
    PgMemReportTest::SetUp();
  }
};

TEST_F(PgMemReportOverBudgetTest, FlagsProcessAndLogsHeapSnapshot) {
  {
    auto conn = ASSERT_RESULT(Connect());
    // Allocates a 20 MB string, which raises the backend's peak RSS.
    ASSERT_OK(conn.Fetch("SELECT length(repeat('x', 20000000))"));
  }
  auto report = ASSERT_RESULT(StopPostgresAndCollect());
  auto& collector = PgMemReportCollector::Get();
  LOG(INFO) << report.ToString();
  ASSERT_FALSE(report.violations.empty());
  ASSERT_STR_CONTAINS(report.ToString(), "client_backend");
  ASSERT_STR_CONTAINS(report.ToString(), "peak grew after: SELECT length(repeat('x', 20000000))");

  bool found_snapshot = false;
  for (const auto& line : collector.Lines()) {
    if (line.find("YB_PG_MEM_SNAPSHOT") != std::string::npos) {
      auto fields = ASSERT_RESULT(ParsePgMemLine(line));
      ASSERT_EQ(fields["kind"], "peak") << line;
      found_snapshot = true;
    }
  }
  ASSERT_TRUE(found_snapshot);
}

class PgMemReportOffTest : public PgMemReportTest {
 protected:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_ysql_pg_mem_report) = false;
    PgMemReportTest::SetUp();
  }
};

TEST_F(PgMemReportOffTest, NoRecords) {
  {
    auto conn = ASSERT_RESULT(Connect());
    ASSERT_OK(conn.Fetch("SELECT 1"));
  }
  auto report = ASSERT_RESULT(StopPostgresAndCollect());
  ASSERT_EQ(report.num_records, 0);
  ASSERT_TRUE(PgMemReportCollector::Get().Lines().empty());
}

}  // namespace yb::pgwrapper
