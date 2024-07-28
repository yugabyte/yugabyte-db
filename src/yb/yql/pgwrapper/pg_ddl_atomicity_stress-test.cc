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

#include <condition_variable>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <vector>
#include <utility>

#include "yb/client/client_fwd.h"
#include "yb/client/table_info.h"
#include "yb/client/yb_table_name.h"
#include "yb/client/client-test-util.h"

#include "yb/common/common.pb.h"
#include "yb/common/pgsql_error.h"
#include "yb/common/schema.h"

#include "yb/master/master_client.pb.h"

#include "yb/util/async_util.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/monotime.h"
#include "yb/util/string_util.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/timestamp.h"
#include "yb/util/tsan_util.h"

#include "yb/yql/pgwrapper/libpq_test_base.h"
#include "yb/yql/pgwrapper/libpq_utils.h"
#include "yb/yql/pgwrapper/pg_ddl_atomicity_test_base.h"
#include "yb/yql/pgwrapper/pg_test_utils.h"

const auto kTable = "test_table";

namespace yb {
namespace pgwrapper {

class PgDdlAtomicityStressTest : public PgDdlAtomicityTestBase {
 protected:
  void UpdateMiniClusterOptions(ExternalMiniClusterOptions* options) override {
    options->extra_tserver_flags.push_back("--yb_enable_read_committed_isolation=false");
    options->extra_tserver_flags.push_back("--ysql_pg_conf_csv=log_statement=all");
    options->extra_master_flags.push_back("--vmodule=ysql_ddl_handler=3,rwc_lock=1");
    options->extra_master_flags.push_back("--ysql_ddl_transaction_wait_for_ddl_verification=false");
  }

  Status StressTestWithFlag(const std::string& error_probability);

  virtual Status SetupTables();

  virtual Result<PGConn> Connect() {
    return LibPqTestBase::Connect();
  }

  virtual int NumIterations() {
    return RegularBuildVsSanitizers(10, 5);
  }

  virtual std::string database() {
    return "yugabyte";
  }

 private:
  Status StressTest();

  Status TestDdl(const std::vector<std::string>& ddl, const int iteration);

  Status TestConcurrentIndex(const int num_iterations);

  Status TestDml(const int num_iterations);

  template<class... Args>
  Result<bool> ExecuteFormatWithRetry(const std::string& format, Args&&... args) {
    return DoExecuteWithRetry(Format(format, std::forward<Args>(args)...));
  }

  Result<bool> DoExecuteWithRetry(const std::string& stmt);

  Status InsertTestData(const int num_rows);
};

Status PgDdlAtomicityStressTest::SetupTables() {
  auto conn = VERIFY_RESULT(Connect());
  return conn.Execute(CreateTableStmt(kTable));
}

Status PgDdlAtomicityStressTest::InsertTestData(const int num_rows) {
  auto conn = VERIFY_RESULT(Connect());
  return conn.ExecuteFormat(
    "INSERT INTO $0 VALUES (generate_series(1, $1))",
    kTable,
    num_rows);
}

Status PgDdlAtomicityStressTest::StressTestWithFlag(const std::string& error_probability) {
  RETURN_NOT_OK(SetupTables());

  RETURN_NOT_OK(InsertTestData(NumIterations() * 2));

  if (!error_probability.empty()) {
    RETURN_NOT_OK(cluster_->SetFlagOnMasters(error_probability, "0.1"));
  }

  RETURN_NOT_OK(StressTest());

  return Status::OK();
}

Status PgDdlAtomicityStressTest::StressTest() {
  const int num_iterations = NumIterations();
  TestThreadHolder thread_holder;

  // We test creation/deletion together so that we can be sure that the entity we are dropping
  // exists when it is executed.

  // Create a thread to add and drop columns.
  thread_holder.AddThreadFunctor([this, num_iterations] {
    std::vector<std::string> ddls = {
      "ALTER TABLE $0 ADD COLUMN col_$1 TEXT",
      "ALTER TABLE $0 DROP COLUMN col_$1"
    };
    ASSERT_OK(TestDdl(ddls, num_iterations));
    LOG(INFO) << "Thread to add and drop columns has completed";
  });

  // Create a thread to add and drop columns with default values.
  thread_holder.AddThreadFunctor([this, num_iterations] {
    std::vector<std::string> ddls = {
      "ALTER TABLE $0 ADD COLUMN col_def_$1 TEXT DEFAULT 'def'",
      "ALTER TABLE $0 DROP COLUMN col_def_$1"
    };
    ASSERT_OK(TestDdl(ddls, num_iterations));
    LOG(INFO) << "Thread to add and drop columns with default values has completed";
  });

  // Create a thread to create/drop an index on this table.
  thread_holder.AddThreadFunctor([this, num_iterations] {
    std::vector<std::string> ddls = {
      "CREATE INDEX NONCONCURRENTLY non_concurrent_idx_$1 ON $0(key)",
      "DROP INDEX non_concurrent_idx_$1"
    };
    ASSERT_OK(TestDdl(ddls, num_iterations));
    LOG(INFO) << "Thread to create/drop an index has completed";
  });

  // ConcurrentIndex is a very long running operation. Cleaning up a failed ConcurrentIndex is
  // also a DDL, and this can be a very long running test. Reduce the number of iterations.
  thread_holder.AddThreadFunctor([this, num_iterations] {
    ASSERT_OK(TestConcurrentIndex(num_iterations / 2));
    LOG(INFO) << "Thread to run concurrent index has completed";
  });

  // Create a thread to update the rows on this table.
  thread_holder.AddThreadFunctor([this, num_iterations] {
    ASSERT_OK(TestDml(num_iterations));
    LOG(INFO) << "Thread to update the rows has completed";
  });

  // Wait for all threads to complete.
  thread_holder.JoinAll();

  LOG(INFO) << "Verify that the table does not contain any additional columns";
  auto client = VERIFY_RESULT(cluster_->CreateClient());
  RETURN_NOT_OK(VerifySchema(client.get(), database(), kTable, {"key", "value", "num"}));

  LOG(INFO) << "Verify that no indexes are present on this table";
  for (int i = 0; i < num_iterations; ++i) {
    client::VerifyTableNotExists(client.get(), database(), Format("idx_$0", i), 10);
  }

  LOG(INFO) << "Verify that all the rows on this table are updated correctly";
  auto conn = VERIFY_RESULT(Connect());
  for (int i = 1; i <= num_iterations; ++i) {
    auto res = VERIFY_RESULT(conn.FetchFormat("SELECT value FROM $0 WHERE key = $1", kTable, i));
    auto num_rows = PQntuples(res.get());
    if (num_rows != 1) {
      return STATUS_FORMAT(Corruption, "Expected 1 rows for key $0, found $1", i, num_rows);
    }

    if (int num_cols = PQnfields(res.get()) != 1) {
      return STATUS_FORMAT(Corruption, "got unexpected number of columns: $0", num_cols);
    }

    auto expected_val = Format("value_$0", i);
    std::string val = VERIFY_RESULT(GetValue<std::string>(res.get(), 0, 0));
    if (val != expected_val) {
      return STATUS_FORMAT(Corruption, "Expected to get $0 for key $1 but got $2",
                            expected_val, i, val);
    }
  }
  LOG(INFO) << __FUNCTION__ << " done";
  return Status::OK();
}


Status PgDdlAtomicityStressTest::TestDdl(
    const std::vector<std::string>& ddls, const int num_iterations) {
  for (int i = 0; i < num_iterations; ++i) {
    for (const auto& ddl : ddls) {
      auto stmt = Format(ddl, kTable, i);
      LOG(INFO) << "Executing stmt " << stmt;
      while (!VERIFY_RESULT(DoExecuteWithRetry(stmt))) {
        LOG(INFO) << "Retry executing stmt " << stmt;
      }
    }
  }
  return Status::OK();
}

Result<bool> PgDdlAtomicityStressTest::DoExecuteWithRetry(const std::string& stmt) {
  auto conn = VERIFY_RESULT(Connect());
  auto s = conn.Execute(stmt);
  if (s.ok()) {
    LOG(INFO) << "Execution of stmt " << stmt << " succeeded";
    return true;
  }

  // Check whether the transaction failed for an expected concurrency error.
  const auto msg = s.message().ToBuffer();
  static const auto allowed_msgs = {
    "Catalog Version Mismatch"sv,
    SerializeAccessErrorMessageSubstring(),
    "Restart read required"sv,
    "Transaction aborted"sv,
    "Transaction metadata missing"sv,
    "Unknown transaction, could be recently aborted"sv,
    "Flush: Value write after transaction start"sv,
    "Injected random failure for testing"sv,
    "expired or aborted by a conflict"sv,
    "schema version mismatch for table"sv,
    "marked for deletion in table"sv,
    "Invalid column number"sv,
    kDdlVerificationError
  };
  if (HasSubstring(msg, allowed_msgs)) {
    LOG(INFO) << "Execution of stmt " << stmt << " failed: " << s;
    return false;
  }

  // In some cases, when "Unknown transaction, could be recently aborted" is returned, we don't know
  // whether the transaction failed or succeeded. In such cases, we retry the statement. However,
  // if the original transaction was not aborted, the retry could fail with "already exists" in case
  // ADD COLUMN or CREATE INDEX and "does not exist" in case of DROP COLUMN or DROP INDEX. Thus in
  // such cases, we consider this statement to be a success.
  static const auto failed_retry_msgs = {
    "does not exist"sv,
    "already exists"sv
  };
  if (HasSubstring(msg, failed_retry_msgs)) {
    LOG(INFO) << "Execution of stmt " << stmt << " considered a success: " << s;
    return true;
  }

  // Unexpected error
  LOG(ERROR) << "Execution of stmt " << stmt << " failed: " << s;
  return s;
}

Status PgDdlAtomicityStressTest::TestConcurrentIndex(const int num_iterations) {
  for (int i = 0; i < num_iterations; ++i) {
    bool index_created = false;
    while (!index_created) {
      // If concurrent index creation fails, it does not clean up the invalid index. Thus to
      // make the statement idempotent, drop the index if the create index failed before retrying.
      index_created = VERIFY_RESULT(ExecuteFormatWithRetry(
          "CREATE INDEX idx_$0 ON $1(key)", i, kTable));
      if (!index_created) {
        auto stmt = Format("DROP INDEX IF EXISTS idx_$0", i);
        while (!VERIFY_RESULT(ExecuteFormatWithRetry(stmt))) {
          LOG(INFO) << "Retry executing stmt " << stmt;
        }
      }
    }
    auto stmt = Format("DROP INDEX idx_$0", i);
    while (!VERIFY_RESULT(ExecuteFormatWithRetry(stmt))) {
      LOG(INFO) << "Retry executing stmt " << stmt;
    }
  }
  return Status::OK();
}

Status PgDdlAtomicityStressTest::TestDml(const int num_iterations) {
  auto conn = VERIFY_RESULT(Connect());
  for (int i = 1; i <= num_iterations;) {
    if (VERIFY_RESULT(ExecuteFormatWithRetry(
                      "UPDATE $0 SET value = 'value_$1' WHERE key = $1", kTable, i))) {
      ++i;
    }
  }
  return Status::OK();
}

TEST_F(PgDdlAtomicityStressTest, BasicTest) {
  ASSERT_OK(StressTestWithFlag(""));
}

TEST_F(PgDdlAtomicityStressTest, TestTxnVerificationFailure) {
  ASSERT_OK(StressTestWithFlag("TEST_ysql_ddl_transaction_verification_failure_probability"));
}

TEST_F(PgDdlAtomicityStressTest, TestFailCatalogWrites) {
  ASSERT_OK(StressTestWithFlag(
      "TEST_ysql_fail_probability_of_catalog_writes_by_ddl_verification"));
}

TEST_F(PgDdlAtomicityStressTest, TestFailDdlRollback) {
  ASSERT_OK(StressTestWithFlag("TEST_ysql_ddl_rollback_failure_probability"));
}

TEST_F(PgDdlAtomicityStressTest, TestFailDdlVerification) {
  ASSERT_OK(StressTestWithFlag("TEST_ysql_ddl_verification_failure_probability"));
}

/*
 * Tests on Colocated Tables.
*/
class PgDdlAtomicityColocatedStressTest : public PgDdlAtomicityStressTest {
  virtual Status SetupTables() override;

  virtual std::string database() override {
    return "yugabyte_colocated";
  }

  Result<PGConn> Connect() override {
    return ConnectToDB(database());
  }
};

Status PgDdlAtomicityColocatedStressTest::SetupTables() {
  auto conn_init = VERIFY_RESULT(LibPqTestBase::Connect());
  RETURN_NOT_OK(conn_init.ExecuteFormat("CREATE DATABASE $0 WITH colocated = true", database()));
  return PgDdlAtomicityStressTest::SetupTables();
}

TEST_F(PgDdlAtomicityColocatedStressTest, BasicTest) {
  ASSERT_OK(StressTestWithFlag(""));
}

TEST_F(PgDdlAtomicityColocatedStressTest, TestTxnVerificationFailure) {
  ASSERT_OK(StressTestWithFlag("TEST_ysql_ddl_transaction_verification_failure_probability"));
}

TEST_F(PgDdlAtomicityColocatedStressTest, TestFailCatalogWrites) {
  ASSERT_OK(StressTestWithFlag(
      "TEST_ysql_fail_probability_of_catalog_writes_by_ddl_verification"));
}

TEST_F(PgDdlAtomicityColocatedStressTest, TestFailDdlRollback) {
  ASSERT_OK(StressTestWithFlag("TEST_ysql_ddl_rollback_failure_probability"));
}

TEST_F(PgDdlAtomicityColocatedStressTest, TestFailDdlVerification) {
  ASSERT_OK(StressTestWithFlag("TEST_ysql_ddl_verification_failure_probability"));
}

} // namespace pgwrapper
} // namespace yb
