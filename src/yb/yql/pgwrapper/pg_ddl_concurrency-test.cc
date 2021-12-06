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

#include "yb/integration-tests/yb_mini_cluster_test_base.h"

#include "yb/util/test_thread_holder.h"

#include "yb/yql/pgwrapper/libpq_test_base.h"

namespace yb {
namespace pgwrapper {
namespace {

CHECKED_STATUS AllowedStatus(const Status& s) {
  if (!s.ok()) {
    static const std::initializer_list<std::string> retryable_markers = {
        STATUS(TryAgain, "").CodeAsString(),
        "Restart read required at"
    };
    const auto msg = s.ToString();
    for (const auto& marker : retryable_markers) {
      if (msg.find(marker) != std::string::npos) {
        return Status::OK();
      }
    }
  }
  return s;
}

CHECKED_STATUS RunIndexCreationQueries(PGConn* conn, const std::string table_name) {
  static const std::initializer_list<std::string> queries = {
      "DROP TABLE IF EXISTS $0",
      "CREATE TABLE IF NOT EXISTS $0(k int PRIMARY KEY, v int)",
      "CREATE INDEX IF NOT EXISTS $0_v ON $0(v)"
  };
  for (const auto& query : queries) {
    RETURN_NOT_OK(AllowedStatus(conn->ExecuteFormat(query, table_name)));
  }
  return Status::OK();
}

} // namespace

class PgDDLConcurrencyTest : public LibPqTestBase {
 public:
  int GetNumMasters() const override { return 3; }
};

/*
 * Index creation commits DDL transaction at the middle.
 * Check that this behavior works properly and doesn't produces unexpected errors
 * in case transaction can't be committed (due to massive retry errors
 * caused by aggressive running of DDL in parallel).
 */
TEST_F(PgDDLConcurrencyTest, YB_DISABLE_TEST_IN_TSAN(IndexCreation)) {
  TestThreadHolder thread_holder;
  for (size_t i = 0; i < 3; ++i) {
    thread_holder.AddThreadFunctor([this, &stop = thread_holder.stop_flag(), idx = i] {
      const auto table_name = Format("t$0", idx);
      auto conn = ASSERT_RESULT(Connect());
      while (!stop.load(std::memory_order_acquire)) {
        ASSERT_OK(RunIndexCreationQueries(&conn, table_name));
      }
    });
  }

  const std::string table_name("t");
  auto conn = ASSERT_RESULT(Connect());
  for (size_t i = 0; i < 3; ++i) {
    ASSERT_OK(RunIndexCreationQueries(&conn, table_name));
  }
}

} // namespace pgwrapper
} // namespace yb
