//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//--------------------------------------------------------------------------------------------------

#include "yb/sql/ybsql-test-base.h"

namespace yb {
namespace sql {

class YbSqlInsertTable : public YbSqlTestBase {
 public:
  YbSqlInsertTable() : YbSqlTestBase() {
  }
};

TEST_F(YbSqlInsertTable, TestSqlInsertTableSimple) {
  // Init the simulated cluster.
  NO_FATALS(CreateSimulatedCluster());

  // Get a processor.
  SqlProcessor *processor = GetSqlProcessor();

  // Create the table 1.
  EXEC_VALID_STMT(
    "CREATE TABLE human_resource(id int, name varchar, salary int, primary key(id, name));");

  // INSERT: Valid statement with column list.
  EXEC_VALID_STMT("INSERT INTO human_resource(id, name, salary) VALUES(1, 'Scott Tiger', 100);");

  // INSERT: Invalid CQL statement though it's a valid SQL statement without column list.
  EXEC_INVALID_STMT("INSERT INTO human_resource VALUES(2, 'Scott Tiger', 100);");

  // INSERT: Invalid statement - Duplicate key.
  // IF NOT EXISTS is not implemented yet, so we ignore this test case for now.
  EXEC_VALID_STMT("INSERT INTO human_resource(id, name, salary) VALUES(1, 'Scott Tiger', 100) "
                  "IF NOT EXISTS;");

  // INSERT: Invalid statement - Wrong table name.
  EXEC_INVALID_STMT("INSERT INTO human_resource_wrong(id, name, salary)"
                    "  VALUES(1, 'Scott Tiger', 100);");

  // INSERT: Invalid statement - Mismatch column and argument count.
  EXEC_INVALID_STMT("INSERT INTO human_resource(id, name, salary) VALUES(1, 'Scott Tiger');");

  // INSERT: Invalid statement - Mismatch column and argument count.
  EXEC_INVALID_STMT("INSERT INTO human_resource(id, name, salary)"
                    " VALUES(1, 'Scott Tiger', 100, 200);");

  // INSERT: Invalid statement - Mismatch column and argument count.
  EXEC_INVALID_STMT("INSERT INTO human_resource(id, name, salary) VALUES(1);");

  // INSERT: Invalid statement - Mismatch column and argument count.
  EXEC_INVALID_STMT("INSERT INTO human_resource(id, name) VALUES(1, 'Scott Tiger', 100);");

  // INSERT: Invalid statement - Missing primary key (id).
  EXEC_INVALID_STMT("INSERT INTO human_resource(name, salary) VALUES('Scott Tiger', 100);");

  // INSERT: Invalid statement - Missing primary key (name).
  EXEC_INVALID_STMT("INSERT INTO human_resource(id, salary) VALUES(2, 100);");

  // Because string and numeric datatypes are implicitly compatible, we cannot test datatype
  // mismatch yet. Once timestamp, boolean, ... types are introduced, type incompability should be
  // tested here also.
}

} // namespace sql
} // namespace yb
