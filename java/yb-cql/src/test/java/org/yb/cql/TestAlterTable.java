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
package org.yb.cql;

import java.util.Arrays;
import java.util.ArrayList;
import java.util.Iterator;

import com.datastax.driver.core.PreparedStatement;
import com.datastax.driver.core.BatchStatement;
import com.datastax.driver.core.BoundStatement;
import com.datastax.driver.core.Row;

import com.datastax.driver.core.UDTValue;
import org.junit.BeforeClass;
import org.junit.Test;

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertFalse;
import static org.yb.AssertionWrappers.assertTrue;

import org.yb.YBTestRunner;

import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@RunWith(value=YBTestRunner.class)
public class TestAlterTable extends BaseCQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestAlterTable.class);

  protected class SelectRunnable implements Runnable {
    private boolean failed = false;

    @Override
    public void run() {
      try {
        LOG.info("Select thread started.");

        PreparedStatement stmt = session.prepare("SELECT * FROM t WHERE id = ?;");
        for (int i = 0; i < 10; i++) {
          LOG.info("Executing select statements.");
          for (int j = 1; j <= 100; j++) {
            session.execute(stmt.bind(new Integer(0)));
          }
          Thread.sleep(100);
        }

        LOG.info("Select thread completed.");

      } catch (Exception e) {
        LOG.info("Exception caught: " + e.getMessage());
        failed = true;
      }
    }

    public boolean hasFailures() {
      return failed;
    }
  }

  @Test
  public void testAlterTableBasic() throws Exception {
    LOG.info("Creating table ...");
    session.execute("CREATE TABLE human_resource1(id int primary key, nomen varchar, age int);");

    LOG.info("Adding column ...");
    session.execute("ALTER TABLE human_resource1 ADD job varchar;");

    LOG.info("Inserting record with new schema ...");
    session.execute("INSERT INTO human_resource1(id, nomen, age, job) " +
                    "VALUES(1, 'Akshat', 21, 'Intern');");

    Row row = runSelect("SELECT id, nomen, age, job FROM human_resource1 WHERE id = 1").next();

    assertEquals(1, row.getInt(0));
    assertEquals("Akshat", row.getString(1));
    assertEquals(21, row.getInt(2));
    assertEquals("Intern", row.getString(3));

    LOG.info("Dropping column ...");
    session.execute("ALTER TABLE human_resource1 DROP age;");

    LOG.info("Dropping invalid column ...");
    runInvalidStmt("ALTER TABLE human_resource1 DROP address;");

    runInvalidStmt("SELECT age FROM human_resource1");

    LOG.info("Renaming column ...");
    session.execute("ALTER TABLE human_resource1 RENAME nomen TO name;");

    runInvalidStmt("SELECT nomen FROM human_resource1");

    LOG.info("Renaming key column ...");
    session.execute("ALTER TABLE human_resource1 RENAME id TO id2;");

    Iterator<Row> rows = runSelect("SELECT id2, name, job FROM human_resource1 WHERE id2 = 1");
    row = rows.next();
    assertEquals("Akshat", row.getString(1));
    assertFalse(rows.hasNext());

    LOG.info("With property ...");
    session.execute("ALTER TABLE human_resource1 WITH default_time_to_live=5;");
    runInvalidStmt("ALTER TABLE human_resource1 WITH ttl=\"5\";");

    LOG.info("With invalid property ...");
    runInvalidStmt("ALTER TABLE human_resource1 WITH abcd=\"5\";");

    LOG.info("Adding 2, dropping 1...");
    session.execute("ALTER TABLE human_resource1 ADD c1 int, c2 varchar DROP job;");

    runInvalidStmt("SELECT job FROM human_resource1");

    rows = runSelect("SELECT id2, name, c1, c2 FROM human_resource1 WHERE id2 = 1");
    row = rows.next();
    assertEquals("Akshat", row.getString(1));
    assertFalse(rows.hasNext());
  }

  // Repeatedly add columns then execute batches of prepared inserts, while another thread
  // runs select statements in the background. This is to make sure stale prepared statements
  // are handled properly.
  @Test
  public void testAlterTableBatchPrepare() throws Exception {
    // Setup table.
    LOG.info("Creating table ...");
    session.execute("CREATE TABLE t (id int primary key, v0 int);");

    SelectRunnable selectRunnable = new SelectRunnable();
    Thread selectThread = new Thread(selectRunnable);
    selectThread.start();

    ArrayList<String> cols = new ArrayList<String>();
    cols.add("id");
    cols.add("v0");

    int idx = 0;

    try {
      for (int iter = 0; iter < 10; iter++) {
        String new_col = "v" + Integer.toString(iter + 1);
        cols.add(new_col);

        LOG.info("Adding column.");
        session.execute("ALTER TABLE t ADD " + new_col + " int;");

        LOG.info("Sending batch inserts.");
        StringBuilder sb1 = new StringBuilder();
        StringBuilder sb2 = new StringBuilder();
        for (int j = 0; j < cols.size(); j++) {
          if (j > 0) {
            sb1.append(",");
            sb2.append(",");
          }
          sb1.append(cols.get(j));
          sb2.append("?");
        }
        String s1 = sb1.toString();
        String s2 = sb2.toString();
        String command = "INSERT INTO t (" + s1 + ") VALUES (" + s2 + ");";
        PreparedStatement stmt = session.prepare(command);
        BatchStatement batch = new BatchStatement();
        for (int i = 1; i <= 100; i++) {
          idx++;
          BoundStatement bound = stmt.bind().setInt(0, new Integer(idx));
          for (int j = 1; j < cols.size(); j++) {
            bound = bound.setInt(j, new Integer(idx));
          }
          batch.add(bound);
        }
        session.execute(batch);
      }
    } finally {
      selectThread.join();
    }
    assertFalse(selectRunnable.hasFailures());
  }

  @Test
  public void testAlterTableWithUDT() throws Exception {

    // Creating Table and Type.
    session.execute("CREATE TABLE alter_udt_test(h int primary key);");
    session.execute("CREATE TYPE test_udt(a int, b text);");

    // Inserting records with old schema.
    session.execute("INSERT INTO alter_udt_test(h) VALUES(0);");

    // Adding columns.
    session.execute("ALTER TABLE alter_udt_test ADD v1 test_udt;");
    session.execute("ALTER TABLE alter_udt_test ADD v2 int;");
    session.execute("ALTER TABLE alter_udt_test ADD v3 test_udt;");

    // Checking old row has null values for the new columns.
    Row row = runSelect("SELECT * FROM alter_udt_test WHERE h = 0").next();
    assertEquals(0, row.getInt("h"));
    assertTrue(row.isNull("v1"));
    assertTrue(row.isNull("v2"));
    assertTrue(row.isNull("v3"));

    // Inserting records with new schema.
    session.execute("INSERT INTO alter_udt_test(h, v1, v2, v3) " +
        "VALUES(1, {a : 1, b : 'foo'}, 21, {a : 2, b : 'bar'});");

    // Checking new row.
    row = runSelect("SELECT * FROM alter_udt_test WHERE h = 1").next();
    assertEquals(1, row.getInt("h"));
    UDTValue udt = row.getUDTValue("v1");
    assertEquals(1, udt.getInt("a"));
    assertEquals("foo", udt.getString("b"));
    assertEquals(21, row.getInt("v2"));
    udt = row.getUDTValue("v3");
    assertEquals(2, udt.getInt("a"));
    assertEquals("bar", udt.getString("b"));

    // Drop an UDT column.
    session.execute("ALTER TABLE alter_udt_test DROP v1;");

    // Checking new row.
    row = runSelect("SELECT * FROM alter_udt_test WHERE h = 1").next();
    assertEquals(1, row.getInt("h"));
    assertEquals(21, row.getInt("v2"));
    udt = row.getUDTValue("v3");
    assertEquals(2, udt.getInt("a"));
    assertEquals("bar", udt.getString("b"));
    // Ensure v1 is deleted.
    assertFalse(row.getColumnDefinitions().contains("v1"));
  }

  @Test
  public void testTransactionAndTTLAfterAlterTable() throws Exception {
    LOG.info("Creating table ...");
    session.execute("CREATE TABLE tbl(id int primary key) WITH transactions={'enabled' : 'true'}");

    LOG.info("Alter table ...");
    session.execute("ALTER TABLE tbl WITH default_time_to_live=20");

    LOG.info("Start transaction ...");
    session.execute("BEGIN TRANSACTION" +
                    "  INSERT INTO tbl(id) VALUES (1);" +
                    "  INSERT INTO tbl(id) VALUES (2);" +
                    "END TRANSACTION;");

    assertQuery("select * from tbl", "Row[1]Row[2]");
    LOG.info("Wait for end of time_to_live ...");
    Thread.sleep(22 * 1000);
    assertQuery("select * from tbl", "");
  }

  @Test
  public void testInvalidAlterTable() throws Exception {
    LOG.info("Creating table ...");
    session.execute("CREATE TABLE tbl(id int primary key) WITH transactions={'enabled' : 'true'}");

    LOG.info("Alter table ...");
    runInvalidStmt("ALTER TABLE tbl WITH tablets=1",
                   "Feature Not Supported. Changing the number of tablets is not supported");

    runInvalidStmt("ALTER TABLE tbl WITH transactions={'enabled' : 'false'}",
                   "Invalid SQL Statement. syntax error, unexpected '{'");
  }
}
