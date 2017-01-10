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
  const char *create_stmt =
    "CREATE TABLE human_resource(id int, name varchar, salary int, primary key(id, name));";
  Status s = processor->Run(create_stmt);
  CHECK_OK(s);

  // Start testing INSERT.
  const char *insert_stmt = nullptr;

  // INSERT: Valid statement with column list.
  insert_stmt = "INSERT INTO human_resource(id, name, salary) VALUES(1, 'Scott Tiger', 100);";
  s = processor->Run(insert_stmt);
  CHECK_OK(s);

  // INSERT: Invalid CQL statement though it's a valid SQL statement without column list.
  insert_stmt = "INSERT INTO human_resource VALUES(2, 'Scott Tiger', 100);";
  s = processor->Run(insert_stmt);
  CHECK(!s.ok());

  // INSERT: Invalid statement - Duplicate key.
  // IF NOT EXISTS is not implemented yet, so we ignore this test case for now.
  insert_stmt = "INSERT INTO human_resource(id, name, salary) VALUES(1, 'Scott Tiger', 100) "
                "IF NOT EXISTS;";
  s = processor->Run(insert_stmt);
  CHECK_OK(s);

  // INSERT: Invalid statement - Wrong table name.
  insert_stmt = "INSERT INTO human_resource_wrong(id, name, salary) VALUES(1, 'Scott Tiger', 100);";
  s = processor->Run(insert_stmt);
  CHECK(!s.ok());

  // INSERT: Invalid statement - Mismatch column and argument count.
  insert_stmt = "INSERT INTO human_resource(id, name, salary) VALUES(1, 'Scott Tiger');";
  s = processor->Run(insert_stmt);
  CHECK(!s.ok());

  // INSERT: Invalid statement - Mismatch column and argument count.
  insert_stmt = "INSERT INTO human_resource(id, name, salary) VALUES(1, 'Scott Tiger', 100, 200);";
  s = processor->Run(insert_stmt);
  CHECK(!s.ok());

  // INSERT: Invalid statement - Mismatch column and argument count.
  insert_stmt = "INSERT INTO human_resource(id, name, salary) VALUES(1);";
  s = processor->Run(insert_stmt);
  CHECK(!s.ok());

  // INSERT: Invalid statement - Mismatch column and argument count.
  insert_stmt = "INSERT INTO human_resource(id, name) VALUES(1, 'Scott Tiger', 100);";
  s = processor->Run(insert_stmt);
  CHECK(!s.ok());

  // INSERT: Invalid statement - Missing primary key (id).
  insert_stmt = "INSERT INTO human_resource(name, salary) VALUES('Scott Tiger', 100);";
  s = processor->Run(insert_stmt);
  CHECK(!s.ok());

  // INSERT: Invalid statement - Missing primary key (name).
  insert_stmt = "INSERT INTO human_resource(id, salary) VALUES(2, 100);";
  s = processor->Run(insert_stmt);
  CHECK(!s.ok());

  // Because string and numeric datatypes are implicitly compatible, we cannot test datatype
  // mismatch yet. Once timestamp, boolean, ... types are introduced, type incompability should be
  // tested here also.
}

} // namespace sql
} // namespace yb
