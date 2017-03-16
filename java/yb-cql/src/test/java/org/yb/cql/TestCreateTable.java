// Copyright (c) YugaByte, Inc.
package org.yb.cql;

import com.datastax.driver.core.ResultSet;
import com.datastax.driver.core.Row;

import org.junit.Test;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

public class TestCreateTable extends TestBase {

  @Test
  public void testCreateTable() throws Exception {
    LOG.info("Begin test");

    // Test create table multiple times (ENG-945)
    for (int i = 0; i < 5; i++) {

      // Create table
      String create_stmt = "CREATE TABLE test_create " +
                           "(h1 int, h2 varchar, r1 int, r2 varchar, v1 int, v2 varchar, " +
                           "primary key ((h1, h2), r1, r2));";
      session.execute(create_stmt);

      // Insert one row. Deliberately insert with same hash key but different range column values.
      String insert_stmt = String.format("INSERT INTO test_create (h1, h2, r1, r2, v1, v2) " +
                                         "VALUES (1, 'a', %s, 'b', %s, 'c');", i, i + 1);
      session.execute(insert_stmt);

      // Select row by the hash key from the test table. Expect only one row returned and not
      // any from previous instances of the table.
      String select_stmt = "SELECT h1, h2, r1, r2, v1, v2 FROM test_create " +
                           "WHERE h1 = 1 AND h2 = 'a';";
      ResultSet rs = session.execute(select_stmt);
      Row row = rs.one();
      assertEquals(1, row.getInt("h1"));
      assertEquals("a", row.getString("h2"));
      assertEquals(i, row.getInt("r1"));
      assertEquals("b", row.getString("r2"));
      assertEquals(i + 1, row.getInt("v1"));
      assertEquals("c", row.getString("v2"));
      assertNull(rs.one());

      String drop_stmt = "DROP TABLE test_create;";
      session.execute(drop_stmt);
    }

    LOG.info("End test");
  }

  @Test
  public void testCreateTableWithAllDatatypes() throws Exception {
    LOG.info("Begin test");

    // Create table
    String create_stmt = "CREATE TABLE test_create " +
                         "(c1 tinyint, c2 smallint, c3 integer, c4 bigint, " +
                         "c5 float, c6 double, " +
                         "c7 varchar, " +
                         "c8 boolean, " +
                         "c9 timestamp, " +
                         "c10 inet, " +
                         "primary key (c1));";
    session.execute(create_stmt);

    String drop_stmt = "DROP TABLE test_create;";
    session.execute(drop_stmt);

    LOG.info("End test");
  }

  @Test
  public void testCreateTableSystemNamespace() throws Exception {
    RunInvalidStmt("CREATE TABLE system.abc (c1 int, PRIMARY KEY(c1));");
  }
}
