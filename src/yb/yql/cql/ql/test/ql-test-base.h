//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------

#pragma once

#include "yb/qlexpr/ql_rowblock.h"

#include "yb/gutil/bind.h"

#include "yb/integration-tests/mini_cluster.h"
#include "yb/master/mini_master.h"

#include "yb/server/server_fwd.h"

#include "yb/util/async_util.h"
#include "yb/util/test_util.h"

#include "yb/yql/cql/ql/ql_fwd.h"
#include "yb/yql/cql/ql/ptree/ptree_fwd.h"
#include "yb/yql/cql/ql/util/statement_params.h"
#include "yb/yql/cql/ql/util/statement_result.h"
#include "yb/yql/cql/ql/util/util_fwd.h"

namespace yb {
namespace ql {

#define ANALYZE_VALID_STMT(stmt, parse_tree)   \
  do {                                         \
    Status s = TestAnalyzer(stmt, parse_tree); \
    EXPECT_TRUE(s.ok());                       \
  } while (false)

#define ANALYZE_INVALID_STMT(stmt, parse_tree) \
  do {                                         \
    Status s = TestAnalyzer(stmt, parse_tree); \
    EXPECT_FALSE(s.ok());                      \
  } while (false)

#define PARSE_VALID_STMT(stmt)                 \
  do {                                         \
    Status s = TestParser(stmt);               \
    EXPECT_TRUE(s.ok());                       \
  } while (false)

#define PARSE_INVALID_STMT(stmt)               \
  do {                                         \
    Status s = TestParser(stmt);               \
    EXPECT_FALSE(s.ok());                      \
  } while (false)

#define PARSE_INVALID_STMT_ERR(stmt, err_msg)  \
  do {                                         \
    Status s = TestParser(stmt);               \
    EXPECT_FALSE(s.ok());                      \
    EXPECT_FALSE(s.ToString().find(err_msg) == string::npos); \
  } while (false)

#define PROCESSOR_RUN(result, stmt)            \
    LOG(INFO) << "Run: " << stmt;              \
    Status result = processor->Run(stmt);

#define EXEC_VALID_STMT(stmt)                  \
  do {                                         \
    PROCESSOR_RUN(s, stmt);                    \
    EXPECT_TRUE(s.ok());                       \
  } while (false)

#define EXEC_INVALID_STMT_WITH_ERROR(stmt, err_msg)           \
  do {                                                        \
    PROCESSOR_RUN(s, stmt);                                   \
    EXPECT_FALSE(s.ok());                                     \
    EXPECT_FALSE(s.ToString().find(err_msg) == string::npos); \
  } while (false)

#define EXEC_INVALID_STMT(stmt) EXEC_INVALID_STMT_WITH_ERROR(stmt, "")

#define CHECK_VALID_STMT(stmt)                 \
  do {                                         \
    PROCESSOR_RUN(s, stmt);                    \
    CHECK(s.ok()) << "Failure: " << s;         \
  } while (false)

#define CHECK_INVALID_STMT(stmt)               \
  do {                                         \
    PROCESSOR_RUN(s, stmt);                    \
    CHECK(!s.ok()) << "Expect failure";        \
  } while (false)

class ClockHolder {
 protected:
  ClockHolder();

  server::ClockPtr clock_;
};

class TestQLProcessor : public ClockHolder {
 public:
  // Public types.
  typedef std::unique_ptr<TestQLProcessor> UniPtr;
  typedef std::unique_ptr<const TestQLProcessor> UniPtrConst;

  // Constructors.
  TestQLProcessor(client::YBClient* client,
                  std::shared_ptr<client::YBMetaDataCache> cache,
                  const RoleName& role_name);
  virtual ~TestQLProcessor();

  void RunAsyncDone(
      Callback<void(const Status&)> cb, const Status& s,
      const ExecutedResultPtr& result) {
    result_ = result;
    cb.Run(s);
  }

  void RunAsync(
      const std::string& stmt, const StatementParameters& params, Callback<void(const Status&)> cb);

  // Execute a QL statement.
  Status Run(
      const std::string& stmt, const StatementParameters& params = StatementParameters());

  Status Run(const Statement& stmt, const StatementParameters& params);

  // Construct a row_block and send it back.
  std::shared_ptr<qlexpr::QLRowBlock> row_block() const;

  const ExecutedResultPtr& result() const { return result_; }

  const RowsResult* rows_result() const;

  std::string CurrentKeyspace() const;

  Status UseKeyspace(const std::string& keyspace_name);

  void RemoveCachedTableDesc(const client::YBTableName& table_name);

  const ParseTreePtr& GetLastParseTree() const {
    return parse_tree_;
  }

  QLProcessor& ql_processor() {
    return *processor_;
  }

  const TreeNodePtr& GetLastParseTreeRoot() const;

 private:
  void RunAsyncInternal(const std::string& stmt, const StatementParameters& params,
                        StatementExecutedCallback cb, bool reparsed = false);

  std::unique_ptr<QLProcessor> processor_;

  // Execute result.
  ExecutedResultPtr result_;

  ParseTreePtr parse_tree_;
};

// Base class for all QL test cases.
class QLTestBase : public YBTest {
 public:
  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  QLTestBase();
  ~QLTestBase();

  //------------------------------------------------------------------------------------------------
  // Test start and cleanup functions.
  void SetUp() override {
    YBTest::SetUp();
  }

  void TearDown() override;

  //------------------------------------------------------------------------------------------------
  // Test only the parser.
  Status TestParser(const std::string& stmt);

  // Tests parser and analyzer
  Status TestAnalyzer(const std::string& stmt, ParseTreePtr* parse_tree);

  //------------------------------------------------------------------------------------------------
  // Create simulated cluster.
  void CreateSimulatedCluster(int num_tablet_servers = 1);

  // Create ql processor.
  TestQLProcessor* GetQLProcessor(const RoleName& role_name = "");


  //------------------------------------------------------------------------------------------------
  // Utility functions for QL tests.

  void VerifyPaginationSelect(TestQLProcessor* processor,
                              const std::string &select_query,
                              int page_size,
                              const std::string expected_rows);

 protected:
  //------------------------------------------------------------------------------------------------

  // Simulated cluster.
  std::shared_ptr<MiniCluster> cluster_;

  // Simulated YB client.
  std::unique_ptr<client::YBClient> client_;
  std::shared_ptr<client::YBMetaDataCache> metadata_cache_;

  // QL Processor.
  std::vector<TestQLProcessor::UniPtr> ql_processors_;

  static const std::string kDefaultKeyspaceName;
};

}  // namespace ql
}  // namespace yb
