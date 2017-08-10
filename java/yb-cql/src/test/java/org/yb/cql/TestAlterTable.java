// Copyright (c) YugaByte, Inc.
package org.yb.cql;

import java.util.Arrays;
import java.util.ArrayList;
import java.util.Iterator;

import com.datastax.driver.core.PreparedStatement;
import com.datastax.driver.core.BatchStatement;
import com.datastax.driver.core.BoundStatement;
import com.datastax.driver.core.Row;

import org.junit.BeforeClass;
import org.junit.Test;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;

public class TestAlterTable extends BaseCQLTest {

  protected class SelectRunnable implements Runnable {
    private boolean failed = false;

    @Override
    public void run() {
      try {
        LOG.info("Select thread started.");

        for (int i = 0; i < 10; i++) {
          PreparedStatement stmt = session.prepare("SELECT * FROM t WHERE id = ?;");
          BatchStatement batch = new BatchStatement();
          for (int j = 1; j <= 100; j++) {
            batch.add(stmt.bind(new Integer(0)));
          }
          LOG.info("Sending batch select.");
          session.execute(batch);
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
}
