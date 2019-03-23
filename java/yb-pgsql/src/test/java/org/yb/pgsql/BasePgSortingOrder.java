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

package org.yb.pgsql;

import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.util.YBTestRunnerNonTsanOnly;

import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.List;
import java.util.Arrays;

import static org.yb.AssertionWrappers.*;

@RunWith(value=YBTestRunnerNonTsanOnly.class)
public class BasePgSortingOrder extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(BasePgSortingOrder.class);

  private static String supportedTypes[] = {
    "BIGSERIAL",
    "BIT (10)",
    "BIT VARYING (10)",
    "VARBIT (10)",
    "BOOLEAN",
    "BOX",
    "BYTEA",
    "CHAR (10)",
    "CHARACTER (10)",
    "VARCHAR (10)",
    "CHARACTER VARYING (10)",
    "CIDR",
    "CIRCLE",
    "DATE",
    "DOUBLE PRECISION",
    "INET",
    "INT",
    "JSON",
    "JSONB",
    "LINE",
    "LSEG",
    "MACADDR",
    "MACADDR8",
    "MONEY",
    "NUMERIC (10, 3)",
    "PATH",
    "PG_LSN",
    "POINT",
    "POLYGON",
    "REAL",
    "SMALLINT",
    "SMALLSERIAL",
    "SERIAL",
    "TEXT",
    "TIME (3) WITHOUT TIME ZONE",
    "TIME (3) WITH TIME ZONE",
    "TIMESTAMP (3) WITHOUT TIME ZONE",
    "TIMESTAMP (3) WITH TIME ZONE",
    "TSQUERY",
    "TSVECTOR",
    "TXID_SNAPSHOT",
    "UUID",
    "XML"
  };

  public String formTableName(String typeName) {
    String name = typeName.replaceAll("[ ()]", "_");
    return name;
  }

  public void createTables(String[] typenames) throws SQLException {
    LOG.info("CREATE TABLES - Start");
    for (String type_name : typenames) {
      Arrays.stream(supportedTypes).anyMatch(str -> type_name.equals(str));

      try (Statement statement = connection.createStatement()) {
        String sql = String.format("CREATE TABLE tab_%s" +
                                   "(id bool, datum %s, PRIMARY KEY(id, datum));",
                                   formTableName(type_name), type_name);
        statement.execute(sql);
        LOG.info("CREATED: " + sql);
      }
    }
    LOG.info("CREATE TABLES - Done");
  }

  public void insertValues(String table_name, String[] values) throws Exception {
    LOG.info("INSERT VALUES - Start");

    // Constructing the SQL statement.
    // INSERT INTO tab_xxx VALUES (true, datum_value);
    // - Hash value should always true to keep all data into one tablet.
    // - DocDB should insert and read "datum_value" in ASC order.
    String stmt = "INSERT INTO tab_" + table_name + " VALUES ";
    boolean first = true;
    for (String value : values) {
      if (first) {
        first = false;
      } else {
        stmt += ", ";
      }
      stmt += "(true, " + value + ")";
    }
    stmt += ";";

    // Execute the statement.
    try (Statement statement = connection.createStatement()) {
      LOG.info("EXECUTE: " + stmt);
      statement.execute(stmt);
    }

    LOG.info("INSERT VALUES - Done");
  }

  public void insertInvalidValues(String table_name, String[] values) throws Exception {
    LOG.info("INSERT INVALID VALUES - Start");

    // Constructing the SQL statement.
    String stmtFormat = "INSERT INTO tab_" + table_name + " VALUES (true, %s);";
    for (String value : values) {
      // Execute the statement.
      String stmt = String.format(stmtFormat, value);
      try (Statement statement = connection.createStatement()) {
        runInvalidQuery(statement, stmt);
      }
    }

    LOG.info("INSERT INVALID VALUES - Done");
  }

  public void selectAndCompare(String table_name, int row_count) throws Exception {
    String pgsqlStmt = "SELECT datum FROM tab_" + table_name + " ORDER BY datum;";
    String docdbStmt = "SELECT datum FROM tab_" + table_name + ";";

    // Make sure that DocDB order is exactly the same as Postgres ORDER BY.
    try (Statement statement = connection.createStatement()) {
      List<Row> pgsqlRows;
      List<Row> docdbRows;
      try (ResultSet rs = statement.executeQuery(pgsqlStmt)) {
        pgsqlRows = getRowList(rs);
      }
      try (ResultSet rs = statement.executeQuery(docdbStmt)) {
        docdbRows = getRowList(rs);
      }

      LOG.info("Comparing result for " + table_name +
               "\n  SQL Order = " + pgsqlRows.toString() +
               "\n  DOC Order = " + docdbRows.toString());
      assertEquals(pgsqlRows.size(), row_count);
      assertEquals(docdbRows.size(), row_count);
      assertEquals(pgsqlRows, docdbRows);
    }
  }

  // Run one testcase.
  public void RunTest(String[] typeNames,
                      String[][] values,
                      String[][] invalidValues) throws Exception {
    // Create all table to test if a type is supported.
    createTables(typeNames);

    // Check the sorting order of each datatype.
    int count = typeNames.length;
    for (int i = 0; i < count; i++) {
      String typeName = typeNames[i];
      String table_name = formTableName(typeName);
      LOG.info("Testing sorting order for " + typeName);

      // Insert values.
      insertValues(table_name, values[i]);
      insertInvalidValues(table_name, invalidValues[i]);

      // Check the ordering in DocDB.
      selectAndCompare(table_name, values[i].length);
    }
  }

  @Override
  public int getTestMethodTimeoutSec() {
    return 1800;
  }
}
