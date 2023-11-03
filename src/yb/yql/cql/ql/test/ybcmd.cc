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
// This module is to help testing Ybcmd Parser manually. This application reads a statement from
// stdin, parses it, and returns result or reports errors.
//
// To connect to an existing cluster at given masters' addresses:
//
// $ ybcmd --ybcmd_run=1 --ybcmd_master_addresses 127.0.0.1:7101,127.0.0.1:7102,127.0.0.1:7103
// ybcmd > create keyspace test;
// ybcmd > use test;
// ybcmd > create table t (c int primary key);
// ybcmd > insert into t (c) values (1);
// ybcmd > select * from t where c = 1;
// ybcmd > exit;
//
// To start a simulated cluster of its own:
//
// $ ybcmd --ybcmd_run=1
// ...
// ybcmd > ...
//
//--------------------------------------------------------------------------------------------------

#include <cstddef>

#include "yb/client/client.h"
#include "yb/client/meta_data_cache.h"

#include "yb/util/result.h"
#include "yb/util/status_log.h"

#include "yb/yql/cql/ql/test/ql-test-base.h"
#include "yb/util/flags.h"

using std::cout;
using std::cin;
using std::endl;
using std::string;
using yb::client::YBClientBuilder;

DEFINE_NON_RUNTIME_bool(ybcmd_run, false, "Not to run this test unless instructed");

DEFINE_NON_RUNTIME_string(ybcmd_master_addresses, "",
              "Comma-separated addresses of the existing masters ybcmd to connect to. If unset, "
              "ybcmd will start a simulated cluster instead.");

namespace yb {
namespace ql {

class TestQLCmd : public QLTestBase {
 public:
  TestQLCmd() : QLTestBase() {
  }

  void ConnectCluster(const string& master_addresses) {
    YBClientBuilder builder;
    builder.add_master_server_addr(master_addresses);
    builder.default_rpc_timeout(MonoDelta::FromSeconds(30));
    client_ = CHECK_RESULT(builder.Build());
    metadata_cache_ = std::make_shared<client::YBMetaDataCache>(client_.get(), false);
  }
};

TEST_F(TestQLCmd, TestQLCmd) {
  if (!FLAGS_ybcmd_run) {
    return;
  }

  if (!FLAGS_ybcmd_master_addresses.empty()) {
    // Connect to external cluster.
    ConnectCluster(FLAGS_ybcmd_master_addresses);
  } else {
    // Init the simulated cluster.
    ASSERT_NO_FATALS(CreateSimulatedCluster());
  }

  // Get a processor.
  TestQLProcessor *processor = GetQLProcessor();

  const string exit_cmd = "exit";
  while (!cin.eof()) {
    // Read the statement.
    string stmt;
    while (!cin.eof()) {
      cout << endl << "\033[1;33mybcmd > \033[0m";

      string sub_stmt;
      getline(cin, sub_stmt);
      stmt += sub_stmt;

      if (stmt.substr(0, 4) == exit_cmd &&
          (stmt[4] == '\0' || isspace(stmt[4]) || stmt[4] == ';')) {
        return;
      }

      if (sub_stmt.find_first_of(";") != string::npos) {
        break;
      }

      if (stmt.size() != 0) {
        stmt += "\n";
      }
    }

    if (stmt.empty()) {
      continue;
    }

    // Execute.
    cout << "\033[1;34mExecute statement: " << stmt << "\033[0m" << endl;
    StatementParameters params;
    do {
      Status s = processor->Run(stmt, params);
      if (!s.ok()) {
        cout << s.ToString(false);
      } else {
        const ExecutedResult::SharedPtr& result = processor->result();
        if (result != nullptr) {
          // Check result.
          switch (result->type()) {
            case ExecutedResult::Type::SET_KEYSPACE:
              cout << "Keyspace set to "
                << static_cast<SetKeyspaceResult*>(result.get())->keyspace();
              break;
            case ExecutedResult::Type::ROWS: {
              RowsResult* rows_result = static_cast<RowsResult*>(result.get());
              auto row_block = rows_result->GetRowBlock();
              cout << row_block->ToString();
              // Extract the paging state from the result (if present) and populate it in the
              // statement parameters to retrieve the next set of rows until the end is reached
              // when there is no more table id in the paging state (below).
              CHECK_OK(params.SetPagingState(rows_result->paging_state()));
              break;
            }
            case ExecutedResult::Type::SCHEMA_CHANGE:
              break;
          }
        }
      }
    } while (!params.table_id().empty());
  }
}

} // namespace ql
} // namespace yb
