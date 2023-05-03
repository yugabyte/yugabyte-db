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

#include "yb/yql/cql/ql/test/ql-test-base.h"

#include "yb/client/client.h"
#include "yb/client/meta_data_cache.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"

#include "yb/util/async_util.h"
#include "yb/util/result.h"
#include "yb/util/status_log.h"

#include "yb/yql/cql/ql/ql_processor.h"
#include "yb/yql/cql/ql/statement.h"

DECLARE_bool(use_cassandra_authentication);

namespace yb {
namespace ql {

using std::string;
using std::vector;
using std::shared_ptr;
using client::YBClientBuilder;

ClockHolder::ClockHolder() : clock_(new server::HybridClock()) {
  CHECK_OK(clock_->Init());
}

//--------------------------------------------------------------------------------------------------
const string QLTestBase::kDefaultKeyspaceName("my_keyspace");

QLTestBase::QLTestBase() {
}

QLTestBase::~QLTestBase() {
}

void QLTestBase::TearDown() {
  client_.reset();
  if (cluster_ != nullptr) {
    cluster_->Shutdown();
  }
  YBTest::TearDown();
}

//--------------------------------------------------------------------------------------------------

void QLTestBase::CreateSimulatedCluster(int num_tablet_servers) {
  // Start mini-cluster with given number of tservers (default: 1), config client options
  MiniClusterOptions opts;
  opts.num_tablet_servers = num_tablet_servers;
  cluster_.reset(new MiniCluster(opts));
  ASSERT_OK(cluster_->Start());
  YBClientBuilder builder;
  builder.add_master_server_addr(cluster_->mini_master()->bound_rpc_addr_str());
  builder.default_rpc_timeout(MonoDelta::FromSeconds(30));
  builder.default_admin_operation_timeout(MonoDelta::FromSeconds(60));
  builder.set_tserver_uuid(cluster_->mini_tablet_server(0)->server()->permanent_uuid());
  client_ = ASSERT_RESULT(builder.Build());
  metadata_cache_ = std::make_shared<client::YBMetaDataCache>(
      client_.get(), false /* Update roles' permissions cache */);
  ASSERT_OK(client_->CreateNamespaceIfNotExists(kDefaultKeyspaceName));
}

//--------------------------------------------------------------------------------------------------
static void CallUseKeyspace(const TestQLProcessor::UniPtr& processor, const string& keyspace_name) {
  // Workaround: it's implemented as a separate function just because ASSERT_OK can
  // call 'return void;' what can be incompatible with another return type.
  ASSERT_OK(processor->UseKeyspace(keyspace_name));
}

TestQLProcessor* QLTestBase::GetQLProcessor(const RoleName& role_name) {
  if (!role_name.empty()) {
    CHECK(FLAGS_use_cassandra_authentication);
  }
  if (!client_) {
    CreateSimulatedCluster();
  }

  ql_processors_.emplace_back(new TestQLProcessor(client_.get(), metadata_cache_, role_name));
  CallUseKeyspace(ql_processors_.back(), kDefaultKeyspaceName);
  return ql_processors_.back().get();
}

TestQLProcessor::TestQLProcessor(client::YBClient* client,
                                 std::shared_ptr<client::YBMetaDataCache> cache,
                                 const RoleName& role_name)
    : processor_(std::make_unique<QLProcessor>(
          client, cache, nullptr /* ql_metrics */, nullptr /* parser_pool */, clock_,
          TransactionPoolProvider())) {
  if (!role_name.empty()) {
    processor_->ql_env_.ql_session()->set_current_role_name(role_name);
  }
}

TestQLProcessor::~TestQLProcessor() = default;

void TestQLProcessor::RunAsync(
    const string& stmt, const StatementParameters& params, Callback<void(const Status&)> cb) {
  result_ = nullptr;
  parse_tree_.reset(); // Delete previous parse tree.
  // RunAsyncInternal() works through Reschedule() loop via RunAsyncTask in QLProcessor class.
  // It calls Prepare(string& stmt) on every loop iteration.
  RunAsyncInternal(stmt, params, Bind(&TestQLProcessor::RunAsyncDone, Unretained(this), cb));
}

Status TestQLProcessor::Run(const std::string& stmt, const StatementParameters& params) {
  Synchronizer s;
  RunAsync(stmt, params, s.AsStatusCallback());
  return s.Wait();
}

Status TestQLProcessor::Run(const Statement& stmt, const StatementParameters& params) {
  result_ = nullptr;
  parse_tree_.reset(); // Delete previous parse tree.

  Synchronizer s;
  // Reschedule() loop in QLProcessor class is not used here.
  RETURN_NOT_OK(stmt.ExecuteAsync(
      processor_.get(), params,
      Bind(&TestQLProcessor::RunAsyncDone, Unretained(this), s.AsStatusCallback())));
  return s.Wait();
}

void TestQLProcessor::RunAsyncInternal(const std::string& stmt, const StatementParameters& params,
                                       StatementExecutedCallback cb, bool reparsed) {
  // This method mainly duplicates QLProcessor::RunAsync(), but uses local ParseTree object
  // to make it available for checks in the test after the statement execution.
  const Status s = processor_->Prepare(stmt, &parse_tree_, reparsed);
  if (PREDICT_FALSE(!s.ok())) {
    return cb.Run(s, nullptr /* result */);
  }
  // Do not make a copy of stmt and params when binding to the RunAsyncDone callback because when
  // error occurs due to stale matadata, the statement needs to be reexecuted. We should pass the
  // original references which are guaranteed to still be alive when the statement is reexecuted.
  processor_->ExecuteAsync(
      *parse_tree_, params,
      Bind(&QLProcessor::RunAsyncDone, Unretained(processor_.get()), ConstRef(stmt),
           ConstRef(params), Unretained(parse_tree_.get()), cb));
}

const RowsResult* TestQLProcessor::rows_result() const {
  if (result_ != nullptr && result_->type() == ExecutedResult::Type::ROWS) {
    return static_cast<const RowsResult*>(result_.get());
  }
  return nullptr;
}

const TreeNodePtr& TestQLProcessor::GetLastParseTreeRoot() const {
  return parse_tree_->root();
}

std::string TestQLProcessor::CurrentKeyspace() const {
  return processor_->ql_env_.CurrentKeyspace();
}

Status TestQLProcessor::UseKeyspace(const std::string& keyspace_name) {
  return processor_->ql_env_.UseKeyspace(keyspace_name);
}

void TestQLProcessor::RemoveCachedTableDesc(const client::YBTableName& table_name) {
  processor_->ql_env_.RemoveCachedTableDesc(table_name);
}

std::shared_ptr<qlexpr::QLRowBlock> TestQLProcessor::row_block() const {
  LOG(INFO) << (result_ == NULL ? "Result is NULL." : "Got result.")
            << " Return type = " << static_cast<int>(ExecutedResult::Type::ROWS);
  if (result_ != nullptr && result_->type() == ExecutedResult::Type::ROWS) {
    return std::shared_ptr<qlexpr::QLRowBlock>(
        static_cast<RowsResult*>(result_.get())->GetRowBlock());
  }
  return nullptr;
}

void QLTestBase::VerifyPaginationSelect(TestQLProcessor* processor,
                                        const string &select_query,
                                        int page_size,
                                        const string expected_rows) {
  StatementParameters params;
  params.set_page_size(page_size);
  string rows;
  do {
    CHECK_OK(processor->Run(select_query, params));
    auto row_block = processor->row_block();
    if (row_block->row_count() > 0) {
      rows.append(row_block->ToString());
    } else {
      // Skip appending empty rowblock but verify it happens only at the last fetch.
      EXPECT_TRUE(processor->rows_result()->paging_state().empty());
    }
    if (processor->rows_result()->paging_state().empty()) {
      break;
    }
    CHECK_OK(params.SetPagingState(processor->rows_result()->paging_state()));
  } while (true);
  EXPECT_EQ(expected_rows, rows);
}

Status QLTestBase::TestParser(const std::string& stmt) {
  ParseTree::UniPtr parse_tree;
  return GetQLProcessor()->ql_processor().Parse(stmt, &parse_tree);
}

// Tests parser and analyzer
Status QLTestBase::TestAnalyzer(const string& stmt, ParseTree::UniPtr* parse_tree) {
  QLProcessor& processor = GetQLProcessor()->ql_processor();
  RETURN_NOT_OK(processor.Parse(stmt, parse_tree));
  RETURN_NOT_OK(processor.Analyze(parse_tree));
  return Status::OK();
}

}  // namespace ql
}  // namespace yb
