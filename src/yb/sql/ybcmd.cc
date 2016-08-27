//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
// This module is to help testing YbSql Parser manually. This application reads a statement from
// stdin, parses it, and returns result or reports errors.
//--------------------------------------------------------------------------------------------------

#include <gflags/gflags.h>
#include <glog/logging.h>

#include <wchar.h>
#include <iostream>
#include <cstddef>

#include "yb/sql/ybsql.h"
#include "yb/util/flags.h"
#include "yb/util/init.h"
#include "yb/util/logging.h"

using std::cout;
using std::cin;
using std::endl;
using std::make_shared;
using std::string;

using yb::sql::YbSql;

DECLARE_bool(logtostderr);
int main(int argc, char** argv) {
  FLAGS_logtostderr = true;
  yb::ParseCommandLineFlags(&argc, &argv, true);
  yb::InitGoogleLoggingSafe(argv[0]);

  YbSql ybsql;
  const string exit_cmd = "exit";

  while (1) {
    // Read the statement.
    string sql_stmt;
    while (1) {
      cout << endl << "\033[1;33mybsql > \033[0m";

      string sub_stmt;
      getline(cin, sub_stmt);
      sql_stmt += sub_stmt;
      if (sql_stmt.size() != 0) {
        sql_stmt += "\n";
      }

      if (sql_stmt.substr(0, 4) == exit_cmd && (isspace(sql_stmt[4]) || sql_stmt[4] == ';')) {
        goto exit_ybsql;
      }

      if (sub_stmt.find_first_of(";") != string::npos) {
        break;
      }
    }

    // Execute.
    cout << "\033[1;34mExecute statement: " << sql_stmt << "\033[0m" << endl;
    ybsql.Process(sql_stmt);
  }

exit_ybsql:
  return 0;
}
