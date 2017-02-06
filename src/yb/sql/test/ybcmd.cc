//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
// This module is to help testing YbSql Parser manually. This application reads a statement from
// stdin, parses it, and returns result or reports errors.
//--------------------------------------------------------------------------------------------------

#include <wchar.h>
#include <iostream>
#include <cstddef>

#include "yb/sql/test/ybsql-test-base.h"

using std::cout;
using std::cin;
using std::endl;
using std::make_shared;
using std::string;

// using yb::Status;
DEFINE_bool(ybsql_run, false, "Not to run this test unless instructed");

namespace yb {
namespace sql {

class YbSqlCmd : public YbSqlTestBase {
 public:
  YbSqlCmd() : YbSqlTestBase() {
  }
};

TEST_F(YbSqlCmd, TestSqlCmd) {
  if (!FLAGS_ybsql_run) {
    return;
  }

  // Init the simulated cluster.
  NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  SqlProcessor *processor = GetSqlProcessor();

  const string exit_cmd = "exit";
  while (1) {
    // Read the statement.
    string sql_stmt;
    while (1) {
      cout << endl << "\033[1;33mybsql > \033[0m";

      string sub_stmt;
      getline(cin, sub_stmt);
      sql_stmt += sub_stmt;

      if (sql_stmt.substr(0, 4) == exit_cmd &&
          (sql_stmt[4] == '\0' || isspace(sql_stmt[4]) || sql_stmt[4] == ';')) {
        return;
      }

      if (sub_stmt.find_first_of(";") != string::npos) {
        break;
      }

      if (sql_stmt.size() != 0) {
        sql_stmt += "\n";
      }
    }

    // Execute.
    cout << "\033[1;34mExecute statement: " << sql_stmt << "\033[0m" << endl;
    Status s = processor->Run(sql_stmt);
    if (!s.ok()) {
      cout << s.ToString(false);
    } else {
      // Check rowblock.
      std::shared_ptr<YQLRowBlock> row_block = processor->row_block();
      if (row_block == nullptr) {
        cout << s.ToString(false);
      } else {
        cout << row_block->ToString();
      }
    }
  }
}

} // namespace sql
} // namespace yb
