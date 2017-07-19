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

#ifndef YB_QL_TEST_QL_TEST_BASE_H_
#define YB_QL_TEST_QL_TEST_BASE_H_

#include "yb/ql/ql_processor.h"
#include "yb/ql/util/ql_env.h"

#include "yb/client/client.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/master/mini_master.h"

#include "yb/util/test_util.h"

namespace yb {
namespace ql {

#define ANALYZE_VALID_STMT(ql_stmt, parse_tree)                      \
do {                                                                  \
  Status s = TestAnalyzer(ql_stmt, parse_tree);                      \
  EXPECT_TRUE(s.ok());                                                \
} while (false)

#define ANALYZE_INVALID_STMT(ql_stmt, parse_tree)                    \
do {                                                                  \
  Status s = TestAnalyzer(ql_stmt, parse_tree);                      \
  EXPECT_FALSE(s.ok());                                               \
} while (false)

#define PARSE_VALID_STMT(ql_stmt)             \
do {                                           \
  Status s = TestParser(ql_stmt);             \
  EXPECT_TRUE(s.ok());                         \
} while (false)

#define PARSE_INVALID_STMT(ql_stmt)           \
do {                                           \
  Status s = TestParser(ql_stmt);             \
  EXPECT_FALSE(s.ok());                        \
} while (false)

#define EXEC_VALID_STMT(ql_stmt)              \
do {                                           \
  Status s = processor->Run(ql_stmt);         \
  EXPECT_TRUE(s.ok());                         \
} while (false)

#define EXEC_INVALID_STMT(ql_stmt)            \
do {                                           \
  Status s = processor->Run(ql_stmt);         \
  EXPECT_FALSE(s.ok());                        \
} while (false)

#define CHECK_VALID_STMT(ql_stmt)              \
do {                                            \
  Status s = processor->Run(ql_stmt);          \
  CHECK(s.ok()) << "Failure: " << s.ToString(); \
} while (false)

#define CHECK_INVALID_STMT(ql_stmt)            \
do {                                            \
  Status s = processor->Run(ql_stmt);          \
  CHECK(!s.ok()) << "Expect failure";           \
} while (false)

class TestQLProcessor : public QLProcessor {
 public:
  // Public types.
  typedef std::unique_ptr<TestQLProcessor> UniPtr;
  typedef std::unique_ptr<const TestQLProcessor> UniPtrConst;

  // Constructors.
  explicit TestQLProcessor(
      std::weak_ptr<rpc::Messenger> messenger, std::shared_ptr<client::YBClient> client,
      std::shared_ptr<client::YBMetaDataCache> cache)
      : QLProcessor(messenger, client, cache, nullptr /* ql_metrics */) { }
  virtual ~TestQLProcessor() { }

  void RunAsyncDone(
      Callback<void(const Status&)> cb, const Status& s,
      const ExecutedResult::SharedPtr& result = nullptr) {
    result_ = result;
    cb.Run(s);
  }

  void RunAsync(
      const string& ql_stmt, const StatementParameters& params, Callback<void(const Status&)> cb) {
    result_ = nullptr;
    QLProcessor::RunAsync(
        ql_stmt, params, Bind(&TestQLProcessor::RunAsyncDone, Unretained(this), cb));
  }

  // Execute a QL statement.
  CHECKED_STATUS Run(
      const std::string& ql_stmt, const StatementParameters& params = StatementParameters()) {
    Synchronizer s;
    RunAsync(ql_stmt, params, Bind(&Synchronizer::StatusCB, Unretained(&s)));
    return s.Wait();
  }

  // Construct a row_block and send it back.
  std::shared_ptr<QLRowBlock> row_block() const {
    LOG(INFO) << (result_ == NULL ? "Result is NULL." : "Got result.")
              << " Return type = " << static_cast<int>(ExecutedResult::Type::ROWS);
    if (result_ != nullptr && result_->type() == ExecutedResult::Type::ROWS) {
      return std::shared_ptr<QLRowBlock>(static_cast<RowsResult*>(result_.get())->GetRowBlock());
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

  std::string CurrentKeyspace() const { return ql_env_.CurrentKeyspace(); }

  CHECKED_STATUS UseKeyspace(const std::string& keyspace_name) {
    return ql_env_.UseKeyspace(keyspace_name);
  }

 private:
  // Execute result.
  ExecutedResult::SharedPtr result_;
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
  CHECKED_STATUS TestParser(const std::string& ql_stmt) {
    QLProcessor *processor = GetQLProcessor();
    ParseTree::UniPtr parse_tree;
    return processor->Parse(ql_stmt, &parse_tree);
  }

  // Tests parser and analyzer
  CHECKED_STATUS TestAnalyzer(const string& ql_stmt, ParseTree::UniPtr *parse_tree) {
    QLProcessor *processor = GetQLProcessor();
    RETURN_NOT_OK(processor->Parse(ql_stmt, parse_tree));
    RETURN_NOT_OK(processor->Analyze(ql_stmt, parse_tree));
    return Status::OK();
  }

  //------------------------------------------------------------------------------------------------
  // Create simulated cluster.
  void CreateSimulatedCluster();

  // Create ql processor.
  TestQLProcessor *GetQLProcessor();

 protected:
  //------------------------------------------------------------------------------------------------

  // Simulated cluster.
  std::shared_ptr<MiniCluster> cluster_;

  // Simulated YB client.
  std::shared_ptr<client::YBClient> client_;
  std::shared_ptr<client::YBMetaDataCache> metadata_cache_;

  // QL Processor.
  std::vector<TestQLProcessor::UniPtr> ql_processors_;

  static const std::string kDefaultKeyspaceName;
};

}  // namespace ql
}  // namespace yb

#endif  // YB_QL_TEST_QL_TEST_BASE_H_
