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

#ifndef YB_SQL_TEST_YBSQL_TEST_BASE_H_
#define YB_SQL_TEST_YBSQL_TEST_BASE_H_

#include "yb/sql/sql_processor.h"
#include "yb/sql/util/sql_env.h"

#include "yb/client/client.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/master/mini_master.h"

#include "yb/util/test_util.h"

namespace yb {
namespace sql {

#define ANALYZE_VALID_STMT(sql_stmt, parse_tree)                      \
do {                                                                  \
  Status s = TestAnalyzer(sql_stmt, parse_tree);                      \
  EXPECT_TRUE(s.ok());                                                \
} while (false)

#define ANALYZE_INVALID_STMT(sql_stmt, parse_tree)                    \
do {                                                                  \
  Status s = TestAnalyzer(sql_stmt, parse_tree);                      \
  EXPECT_FALSE(s.ok());                                               \
} while (false)

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

class YbSqlProcessor : public SqlProcessor {
 public:
  // Public types.
  typedef std::unique_ptr<YbSqlProcessor> UniPtr;
  typedef std::unique_ptr<const YbSqlProcessor> UniPtrConst;

  // Constructors.
  explicit YbSqlProcessor(
      std::weak_ptr<rpc::Messenger> messenger, std::shared_ptr<client::YBClient> client,
      std::shared_ptr<client::YBMetaDataCache> cache)
      : SqlProcessor(messenger, client, cache, nullptr /* sql_metrics */) { }
  virtual ~YbSqlProcessor() { }

  void RunAsyncDone(
      Callback<void(const Status&)> cb, const Status& s, const ExecutedResult::SharedPtr& result) {
    result_ = result;
    cb.Run(s);
  }

  void RunAsync(
      const string& sql_stmt, const StatementParameters& params, Callback<void(const Status&)> cb) {
    result_ = nullptr;
    SqlProcessor::RunAsync(
        sql_stmt, params, Bind(&YbSqlProcessor::RunAsyncDone, Unretained(this), cb));
  }

  // Execute a SQL statement.
  CHECKED_STATUS Run(
      const std::string& sql_stmt, const StatementParameters& params = StatementParameters()) {
    Synchronizer s;
    RunAsync(sql_stmt, params, Bind(&Synchronizer::StatusCB, Unretained(&s)));
    return s.Wait();
  }

  // Construct a row_block and send it back.
  std::shared_ptr<YQLRowBlock> row_block() const {
    if (result_ != nullptr && result_->type() == ExecutedResult::Type::ROWS) {
      return std::shared_ptr<YQLRowBlock>(static_cast<RowsResult*>(result_.get())->GetRowBlock());
    }
    return nullptr;
  }

  const ExecutedResult::SharedPtr& result() const { return result_; }

  const RowsResult* rows_result() const {
    if (result_ != nullptr && result_->type() == ExecutedResult::Type::ROWS) {
      return static_cast<const RowsResult*>(result_.get());
    }
    return nullptr;
  }

  std::string CurrentKeyspace() const { return sql_env_.CurrentKeyspace(); }

  CHECKED_STATUS UseKeyspace(const std::string& keyspace_name) {
    return sql_env_.UseKeyspace(keyspace_name);
  }

 private:
  // Execute result.
  ExecutedResult::SharedPtr result_;
};

// Base class for all SQL test cases.
class YbSqlTestBase : public YBTest {
 public:
  //------------------------------------------------------------------------------------------------
  // Constructor and destructor.
  YbSqlTestBase();
  ~YbSqlTestBase();

  //------------------------------------------------------------------------------------------------
  // Test start and cleanup functions.
  virtual void SetUp() override {
    YBTest::SetUp();
  }
  virtual void TearDown() override {
    if (cluster_ != nullptr) {
      cluster_->Shutdown();
    }
    YBTest::TearDown();
  }

  //------------------------------------------------------------------------------------------------
  // Test only the parser.
  CHECKED_STATUS TestParser(const std::string& sql_stmt) {
    SqlProcessor *processor = GetSqlProcessor();
    ParseTree::UniPtr parse_tree;
    return processor->Parse(sql_stmt, &parse_tree, nullptr /* mem_tracker */);
  }

  // Tests parser and analyzer
  CHECKED_STATUS TestAnalyzer(const string& sql_stmt, ParseTree::UniPtr *parse_tree) {
    SqlProcessor *processor = GetSqlProcessor();
    RETURN_NOT_OK(processor->Parse(sql_stmt, parse_tree, nullptr /* mem_tracker */));
    RETURN_NOT_OK(processor->Analyze(sql_stmt, parse_tree));
    return Status::OK();
  }

  //------------------------------------------------------------------------------------------------
  // Create simulated cluster.
  void CreateSimulatedCluster();

  // Create sql processor.
  YbSqlProcessor *GetSqlProcessor();

 protected:
  //------------------------------------------------------------------------------------------------

  // Simulated cluster.
  std::shared_ptr<MiniCluster> cluster_;

  // Simulated YB client.
  std::shared_ptr<client::YBClient> client_;
  std::shared_ptr<client::YBMetaDataCache> metadata_cache_;

  // SQL Processor.
  std::vector<YbSqlProcessor::UniPtr> sql_processors_;

  static const std::string kDefaultKeyspaceName;
};

}  // namespace sql
}  // namespace yb

#endif  // YB_SQL_TEST_YBSQL_TEST_BASE_H_
