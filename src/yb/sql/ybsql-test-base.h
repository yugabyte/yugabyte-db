//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#ifndef YB_SQL_YBSQL_TEST_BASE_H_
#define YB_SQL_YBSQL_TEST_BASE_H_

#include "yb/sql/ybsql.h"
#include "yb/sql/sql_processor.h"
#include "yb/sql/util/sql_env.h"

#include "yb/client/client.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/master/mini_master.h"

#include "yb/util/test_util.h"

namespace yb {
namespace sql {

#define PARSE_VALID_STMT(sql_stmt)             \
do {                                           \
  Status s = TestParser(sql_stmt);             \
  EXPECT_TRUE(s.ok());                         \
} while (false)

#define PARSE_INVALID_STMT(sql_stmt)           \
do {                                           \
  Status s = TestParser(sql_stmt);             \
  EXPECT_FALSE(s.ok());                        \
} while (false)

#define EXEC_VALID_STMT(sql_stmt)              \
do {                                           \
  Status s = processor->Run(sql_stmt);         \
  EXPECT_TRUE(s.ok());                         \
} while (false)

#define EXEC_INVALID_STMT(sql_stmt)            \
do {                                           \
  Status s = processor->Run(sql_stmt);         \
  EXPECT_FALSE(s.ok());                        \
} while (false)

#define CHECK_VALID_STMT(sql_stmt)              \
do {                                            \
  Status s = processor->Run(sql_stmt);          \
  CHECK(s.ok()) << "Failure: " << s.ToString(); \
} while (false)

#define CHECK_INVALID_STMT(sql_stmt)            \
do {                                            \
  Status s = processor->Run(sql_stmt);          \
  CHECK(!s.ok()) << "Expect failure";           \
} while (false)

// Base class for all SQL test cases.
class YbSqlTestBase : public YBTest {
 public:
  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  YbSqlTestBase();
  ~YbSqlTestBase();

  //------------------------------------------------------------------------------------------------
  // Test start and cleanup functions.
  virtual void SetUp() OVERRIDE {
    YBTest::SetUp();
  }
  virtual void TearDown() OVERRIDE {
    if (cluster_ != nullptr) {
      cluster_->Shutdown();
    }
    YBTest::TearDown();
  }

  //------------------------------------------------------------------------------------------------
  // Test only the parser.
  CHECKED_STATUS TestParser(const std::string& sql_stmt) {
    return ybsql_->TestParser(sql_stmt);
  }

  //------------------------------------------------------------------------------------------------
  // Create simulated cluster.
  void CreateSimulatedCluster();

  // Create sql processor.
  SqlProcessor *GetSqlProcessor();

  // Create a session context for client_.
  SqlEnv *CreateSqlEnv(std::shared_ptr<client::YBSession> write_session = nullptr,
                       std::shared_ptr<client::YBSession> read_session = nullptr);

  // Pull a session from the cached tables.
  SqlEnv *GetSqlEnv(int session_id);

  // Create a session context for a new connection.
  SqlEnv *CreateNewConnectionContext();

 protected:
  //------------------------------------------------------------------------------------------------
  static constexpr int kSessionTimeoutMs = 60000;

  // SQL engine.
  YbSql::UniPtr ybsql_;

  // Simulated cluster.
  std::shared_ptr<MiniCluster> cluster_;

  // Simulated YB client.
  std::shared_ptr<client::YBClient> client_;

  // Contexts to be passed to SQL engine.
  std::vector<SqlEnv::UniPtr> sql_envs_;

  // SQL Processor.
  std::vector<SqlProcessor::UniPtr> sql_processors_;
};

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_YBSQL_TEST_BASE_H_
