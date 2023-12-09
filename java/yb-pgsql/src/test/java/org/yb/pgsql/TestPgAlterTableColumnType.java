// Copyright (c) Yugabyte, Inc.
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

import java.sql.ResultSet;
import java.sql.Statement;
import java.util.Arrays;

import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.YBTestRunner;

import static org.yb.AssertionWrappers.*;

@RunWith(value = YBTestRunner.class)
public class TestPgAlterTableColumnType extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgAlterTable.class);

  private double roundToDecimalPlaces(double value, int numDecimalPlaces) {
    return Math.round(value * Math.pow(10, numDecimalPlaces)) / Math.pow(10, numDecimalPlaces);
  }

  @Before
  public void setupTableWithVarcharAndIntColumn() throws Exception {
    try (Statement statement = connection.createStatement()) {
      statement.execute("CREATE TABLE varchar_table(id SERIAL PRIMARY KEY, c1 varchar(10))");
      statement.execute("CREATE TABLE text_table(c1 text)");
      statement.execute("CREATE TABLE int4_table(id SERIAL PRIMARY KEY, c1 int4)");
    }
  }

  @After
  public void cleanupTables() throws Exception {
    try (Statement statement = connection.createStatement()) {
      statement.execute("DROP TABLE varchar_table");
      statement.execute("DROP TABLE text_table");
      statement.execute("DROP TABLE int4_table CASCADE");
    }
  }

  @Test
  public void testWithInvalidConversion() throws Exception {
    try (Statement statement = connection.createStatement()) {
      runInvalidQuery(statement, "ALTER TABLE varchar_table ALTER c1 TYPE int",
          "ERROR: column \"c1\" cannot be cast automatically to type integer" +
              "\n  Hint: You might need to specify \"USING c1::integer\".");

      statement.execute("ALTER TABLE int4_table ALTER c1 TYPE int8");
      statement.execute("INSERT INTO int4_table(c1) VALUES (2 ^ 40)");
      runInvalidQuery(statement, "ALTER TABLE int4_table ALTER c1 TYPE int4",
          "ERROR: integer out of range");

      statement.execute("INSERT INTO varchar_table(c1) VALUES ('aa')");
      runInvalidQuery(statement, "ALTER TABLE varchar_table ALTER c1 TYPE varchar(1)",
          "ERROR: value too long for type character varying(1)");
    }
  }

  @Test
  public void testStringConversion() throws Exception {
    try (Statement statement = connection.createStatement()) {
      statement.execute("INSERT INTO varchar_table(c1) VALUES ('a'), (2), ('aaa')");
      statement.execute("ALTER TABLE varchar_table ALTER c1 TYPE text");

      String[] rows = { "a", "2", "aaa" };
      ResultSet rs = statement.executeQuery("SELECT * from varchar_table ORDER BY id ASC");
      for (int i = 0; i < 3; i++) {
        assertTrue(rs.next());
        assertEquals(rows[i], rs.getString("c1"));
      }
    }
  }

  @Test
  public void testUsingExpression() throws Exception {
    try (Statement statement = connection.createStatement()) {
      statement.execute("INSERT INTO varchar_table(c1) VALUES ('a'), ('bb'), ('ccc')");
      statement.execute("ALTER TABLE varchar_table ALTER c1 TYPE int USING length(c1)");
      ResultSet rs = statement.executeQuery("SELECT * from varchar_table ORDER BY c1 ASC");
      for (int i = 1; i <= 3; i++) {
        assertTrue(rs.next());
        assertEquals(i, rs.getInt("c1"));
      }

      statement.execute("ALTER TABLE varchar_table ALTER c1 TYPE double precision USING " +
          "sqrt(c1)");
      rs = statement.executeQuery("SELECT * from varchar_table ORDER BY c1 ASC");
      for (int i = 1; i <= 3; i++) {
        assertTrue(rs.next());
        assertEquals(roundToDecimalPlaces(Math.sqrt(i), 5),
          roundToDecimalPlaces(rs.getDouble("c1"), 5));
      }
    }
  }

  @Test
  public void testOtherCommands() throws Exception {
    try (Statement statement = connection.createStatement()) {
      statement.execute("ALTER TABLE int4_table ADD COLUMN c2 varchar, ADD COLUMN c3 int, " +
          "ALTER c1 TYPE varchar");
      statement.execute("INSERT INTO int4_table(c1, c2, c3) VALUES ('a', 'a', 1), " +
          "('b', 'b', 2), ('c', 'c', 3)");

      ResultSet rs = statement.executeQuery("SELECT * from int4_table ORDER BY c3 ASC");
      for (int i = 1; i <= 2; i++) {
        assertTrue(rs.next());
        assertEquals(Character.toString((char) ('a' + i - 1)), rs.getString("c1"));
        assertEquals(Character.toString((char) ('a' + i - 1)), rs.getString("c2"));
        assertEquals(i, rs.getInt("c3"));
      }
    }
  }

  @Test
  public void testAlterPartitionColumnTypeFailure() throws Exception {
    try (Statement statement = connection.createStatement()) {
      statement.execute("CREATE TABLE int4_table2(c1 int) PARTITION BY RANGE(c1)");
      runInvalidQuery(statement, "ALTER TABLE int4_table2 ALTER c1 TYPE varchar",
          "ERROR: cannot alter column \"c1\" because it is part of the partition key of relation " +
          "\"int4_table2\"");
    }
  }

  @Test
  public void testRulesFailure() throws Exception {
    try (Statement statement = connection.createStatement()) {
      statement.execute("CREATE RULE r AS ON UPDATE TO int4_table DO ALSO NOTIFY int4_table;");
      runInvalidQuery(statement, "ALTER TABLE int4_table ALTER c1 TYPE varchar",
        "ERROR: changing column type of a table with rules is not yet implemented");
    }
  }

  @Test
  public void testPrimaryKey() throws Exception {
    try (Statement statement = connection.createStatement()) {
      statement.execute("CREATE TABLE pk_table(c1 int primary key)");
      statement.execute("INSERT INTO pk_table VALUES (1), (2)");
      statement.execute("ALTER TABLE pk_table ALTER c1 TYPE varchar");

      ResultSet rs = statement.executeQuery("SELECT * from pk_table ORDER BY c1 ASC");
      for (int i = 1; i <= 2; i++) {
        assertTrue(rs.next());
        assertEquals(i, rs.getInt("c1"));
      }

      runInvalidQuery(statement, "INSERT INTO pk_table VALUES (1)",
          "duplicate key value violates unique constraint \"pk_table_pkey\"");

      runInvalidQuery(statement, "ALTER TABLE pk_table ALTER c1 TYPE int USING length(c1)",
          "duplicate key value violates unique constraint \"pk_table\"");
    }
  }

  @Test
  public void testForeignKey() throws Exception {
    try (Statement statement = connection.createStatement()) {
      statement.execute("CREATE TABLE fk_table(c1 int, c2 int references int4_table(id))");
      statement.execute("ALTER TABLE fk_table ALTER c1 TYPE varchar");

      runInvalidQuery(statement, "ALTER TABLE fk_table ALTER c2 TYPE varchar",
        "ERROR: Altering type of foreign key is not supported");

      runInvalidQuery(statement, "ALTER TABLE int4_table ALTER id TYPE varchar",
        "ERROR: Altering type of foreign key is not supported");

      statement.execute("DROP TABLE fk_table");
    }
  }

  @Test
  public void testCheckConstraints() throws Exception {
    try (Statement statement = connection.createStatement()) {
      statement.execute("CREATE TABLE check_table(c1 int check(c1 > 0))");
      statement.execute("INSERT INTO check_table VALUES (1), (2)");

      runInvalidQuery(statement, "ALTER TABLE check_table ALTER c1 TYPE float USING -c1",
        "ERROR: check constraint \"check_table_c1_check\" is violated by some row");

      statement.execute("ALTER TABLE check_table ALTER c1 TYPE float");

      ResultSet rs = statement.executeQuery("SELECT * from check_table ORDER BY c1 ASC");
      for (int i = 1; i <= 2; i++) {
        assertTrue(rs.next());
        assertEquals(i, rs.getInt("c1"));
      }

      statement.execute("INSERT INTO check_table VALUES(3.0)");

      runInvalidQuery(statement, "INSERT INTO check_table VALUES (-3.0)",
          "new row for relation \"check_table\" violates check constraint " +
              "\"check_table_c1_check\"");
    }
  }

  @Test
  public void testDefaults() throws Exception {
    try (Statement statement = connection.createStatement()) {
      statement.execute("CREATE TABLE default_table(c1 int, c2 varchar default 'a')");

      runInvalidQuery(statement, "ALTER TABLE default_table ALTER c2 TYPE int USING length(c2)",
        "default for column \"c2\" cannot be cast automatically to type integer");

      statement.execute("CREATE TABLE default_table2(c1 int, c2 int default 10,"
        + " c3 text default 'x')");
      // DROP a column to change attribute mapping before running alter type.
      statement.execute("ALTER TABLE default_table2 DROP COLUMN c1");
      statement.execute("ALTER TABLE default_table2 ADD COLUMN c1 int");
      statement.execute("ALTER TABLE default_table2 ALTER c2 TYPE varchar(10)");

      statement.execute("INSERT INTO default_table2(c1) VALUES (1)");

      ResultSet rs = statement.executeQuery("SELECT * from default_table2");
      assertTrue(rs.next());
      assertEquals(1, rs.getInt("c1"));
      // The default is converted automatically, not with the cast expression
      // length(c2).
      assertEquals(Integer.toString(10), rs.getString("c2"));
      assertEquals("x", rs.getString("c3"));

      // Verify that ALTER TYPE + ALTER ... SET DEFAULT works.
      statement.execute("ALTER TABLE default_table2 ALTER c2 TYPE varchar(3),"
        + " ALTER c2 SET DEFAULT 'xyz'");
      statement.execute("INSERT INTO default_table2(c1) VALUES (2)");
      assertRowList(statement, "SELECT * FROM default_table2 ORDER BY c1", Arrays.asList(
          new Row("10", "x", 1),
          new Row("xyz", "x", 2)));
    }
  }

  @Test
  public void testMiscColumnDependencies() throws Exception {
    try (Statement statement = connection.createStatement()) {
      statement.execute("CREATE TYPE test_type AS (t text)");
      statement.execute("CREATE TABLE test_table(c1 int, c2 int, c3 int)");
      statement.execute("INSERT INTO test_table VALUES(1, 2, 3)");
      statement.execute("ANALYZE test_table");
      // DROP a column to change attribute mapping before running alter type.
      statement.execute("ALTER TABLE test_table DROP COLUMN c1");
      // Verify that there's a pg_statistic entry for the column we are about to alter.
      assertQuery(statement, "SELECT count(*) FROM pg_statistic JOIN pg_attribute ON"
        + " starelid=attrelid AND staattnum=attnum WHERE attrelid='test_table'::regclass AND"
        + " attname='c2'", new Row(1));
      statement.execute("ALTER TABLE test_table ALTER c2 TYPE test_type USING"
        + " ROW(c2)::test_type");
      // Verify that the pg_statistic entry for the altered column (c2) was removed.
      assertQuery(statement, "SELECT count(*) FROM pg_statistic JOIN pg_attribute ON"
        + " starelid=attrelid AND staattnum=attnum WHERE attrelid='test_table'::regclass AND"
        + " attname='c2'", new Row(0));
      // Verify that the pg_statistic entry for the other column (c3) still exists.
      assertQuery(statement, "SELECT count(*) FROM pg_statistic JOIN pg_attribute ON"
        + " starelid=attrelid AND staattnum=attnum WHERE attrelid='test_table'::regclass AND"
        + " attname='c3'", new Row(1));
      // Verify that a dependency between the altered column and the type exists.
      assertQuery(statement, "SELECT count(*) FROM pg_depend JOIN pg_attribute ON objid=attrelid"
        + " AND objsubid=attnum AND refobjid=atttypid WHERE attrelid='test_table'::regclass"
        + " AND attname='c2'", new Row(1));
      statement.execute("DROP TYPE test_type CASCADE");
      assertQuery(statement, "SELECT * FROM test_table", new Row(3));
      statement.execute("ALTER TABLE test_table ALTER c3 TYPE text COLLATE \"en_US\"");
      // Verify that the pg_statistic entry for the altered column (c3) was removed.
      assertQuery(statement, "SELECT count(*) FROM pg_statistic JOIN pg_attribute ON"
        + " starelid=attrelid AND staattnum=attnum WHERE attrelid='test_table'::regclass AND"
        + " attname='c3'", new Row(0));
      // Verify that a dependency was created between the altered column and the collation.
      assertQuery(statement, "SELECT count(*) FROM pg_depend JOIN pg_attribute ON objid=attrelid"
        + " AND objsubid=attnum AND refobjid=attcollation WHERE attrelid='test_table'::regclass"
        + " AND attname='c3'", new Row(1));
      statement.execute("DROP COLLATION \"en_US\" CASCADE");
      assertQuery(statement, "SELECT * FROM test_table", new Row());
    }
  }

  @Test
  public void testSplitOptions() throws Exception {
    try (Statement statement = connection.createStatement()) {
      statement.execute("CREATE TABLE test (c1 varchar, c2 varchar) SPLIT INTO 5 TABLETS");
      statement.execute("CREATE INDEX idx1 ON test(c1) SPLIT INTO 5 TABLETS");
      statement.execute("CREATE INDEX idx2 ON test(c1 HASH, c2 ASC) SPLIT INTO 5 TABLETS");
      statement.execute("CREATE INDEX idx3 ON test(c2 ASC) INCLUDE(c1)"
        + " SPLIT AT VALUES ((E'test123\"\"''\\\\\\u0068\\u0069'))");
      statement.execute("CREATE INDEX idx4 ON test(c1 ASC) SPLIT AT VALUES (('h'))");
      statement.execute("ALTER TABLE test ALTER c1 TYPE int USING length(c1)");
      // Hash split options on the table should be preserved.
      assertQuery(statement, "SELECT num_tablets, num_hash_key_columns FROM"
        + " yb_table_properties('test'::regclass)", new Row(5, 1));
      // Hash split options on the indexes should be preserved.
      assertQuery(statement, "SELECT num_tablets, num_hash_key_columns FROM"
        + " yb_table_properties('idx1'::regclass)", new Row(5, 1));
      assertQuery(statement, "SELECT num_tablets, num_hash_key_columns FROM"
        + " yb_table_properties('idx2'::regclass)", new Row(5, 1));
      // Range split options on an index should be preserved only when the altered column
      // is not a part of the index key.
      assertQuery(statement, "SELECT yb_get_range_split_clause('idx3'::regclass)",
          new Row("SPLIT AT VALUES ((E'test123\"\"''\\\\hi'))"));
      assertQuery(statement, "SELECT yb_get_range_split_clause('idx4'::regclass)",
          new Row(""));

      statement.execute("CREATE TABLE test2 (c1 varchar, c2 varchar, PRIMARY KEY(c1 ASC, c2 DESC))"
      + " SPLIT AT VALUES (('h', 20))");
      statement.execute("ALTER TABLE test2 ALTER c1 TYPE int USING length(c1)");
      statement.execute("CREATE TABLE test3 (c1 varchar, c2 varchar, PRIMARY KEY(c2 ASC))"
      + " SPLIT AT VALUES ((E'test123\"\"''\\\\\\u0068\\u0069'))");
      statement.execute("ALTER TABLE test3 ALTER c1 TYPE int USING length(c1)");
      // Range split options on the table should be preserved only when the altered column
      // is not a part of the index key.
      assertQuery(statement, "SELECT yb_get_range_split_clause('test2'::regclass)",
          new Row(""));
      assertQuery(statement, "SELECT yb_get_range_split_clause('test3'::regclass)",
          new Row("SPLIT AT VALUES ((E'test123\"\"''\\\\hi'))"));
    }
  }

  @Test
  public void testMultipleRewrites() throws Exception {
    try (Statement statement = connection.createStatement()) {
      statement.execute("CREATE TABLE test (a float, b varchar(10))");
      statement.execute("INSERT INTO test VALUES (1.0, 'abc'), (2.0, 'xyz')");
      // Test multiple ALTER COLUMN TYPE subcommands.
      statement.execute("ALTER TABLE test ALTER COLUMN a TYPE text USING a::text,"
        + " ALTER COLUMN b TYPE varchar(5);");
      assertRowList(statement, "SELECT * FROM test ORDER BY a", Arrays.asList(
          new Row("1", "abc"),
          new Row("2", "xyz")));
      // Test multiple ALTER COLUMN TYPE subcommands with an ADD PRIMARY KEY subcommand.
      statement.execute("ALTER TABLE test ALTER COLUMN a TYPE varchar(3),"
        + " ALTER COLUMN b TYPE varchar(4), ADD PRIMARY KEY (b)");
      assertRowList(statement, "SELECT * FROM test ORDER BY a", Arrays.asList(
          new Row("1", "abc"),
          new Row("2", "xyz")));
    }
  }
}
