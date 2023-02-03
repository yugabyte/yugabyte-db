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

package org.yb.pgsql;

import static org.yb.AssertionWrappers.*;

import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.Statement;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import org.yb.util.ThreadUtil;
import org.yb.YBTestRunner;

@RunWith(value = YBTestRunner.class)
public class TestPgReadCommittedFuncs extends BasePgSQLTest {
  private static final Logger LOG =
    LoggerFactory.getLogger(TestPgReadCommittedFuncs.class);

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flags = super.getTServerFlags();
    flags.put("yb_enable_read_committed_isolation", "true");
    return flags;
  }

  private String getFunctionDefinitionStr(String volatilityClass) {
    return
      "CREATE OR REPLACE FUNCTION " + volatilityClass + "_plpgsql_func() RETURNS TABLE(v int) " +
      "  AS $$" +
      "    BEGIN " +
      "    RETURN QUERY SELECT test.v FROM test WHERE k=1;" +
      "    PERFORM pg_sleep(2);" +
      "    RETURN QUERY SELECT test.v FROM test WHERE k=1;" +
      "    END;" +
      "  $$ LANGUAGE PLPGSQL " + volatilityClass;
  }

  /**
   *
   * Test to ensure that each statement in a volatile plpgsql function uses a new snapshot and all
   * statements in immutable and stable functions use the same snapshot.
   *
   * Quoting from Pg docs (https://www.postgresql.org/docs/current/xfunc-volatility.html):
   *   "STABLE and IMMUTABLE functions use a snapshot established as of the start of the calling
   *    query, whereas VOLATILE functions obtain a fresh snapshot at the start of each query they
   *    execute."
   */
  @Test
  public void testVolatileFunctionSemantics() throws Exception {
    String[] volatilityClasses = {"VOLATILE", "IMMUTABLE", "STABLE"};
    try (Statement statement = connection.createStatement()) {
      statement.execute("CREATE TABLE test (k INT PRIMARY KEY, v INT)");
      statement.execute("INSERT INTO test VALUES (1, 0)");
      statement.execute(
        "CREATE OR REPLACE PROCEDURE update_row() " +
        "  AS $$" +
        "    DECLARE " +
        "      end_time TIMESTAMP;" +
        "    BEGIN " +
        "    end_time := NOW() + INTERVAL '5 seconds';" +
        "    WHILE end_time > NOW() LOOP" +
        "       UPDATE test SET v = EXTRACT(EPOCH FROM AGE(end_time, NOW())) WHERE k=1;" +
        "       COMMIT;" +
        "    END LOOP;" +
        "    END;" +
        "  $$ LANGUAGE PLPGSQL");
      for (String volatilityClass : volatilityClasses) {
        statement.execute(getFunctionDefinitionStr(volatilityClass));
      }
    }

    List<Runnable> runnables = new ArrayList<>();

    runnables.add(() -> {
      try (Connection conn =
              getConnectionBuilder().withIsolationLevel(IsolationLevel.READ_COMMITTED)
              .withAutoCommit(AutoCommit.ENABLED).connect();
            Statement stmt = conn.createStatement();) {
        stmt.execute("CALL update_row();");
      } catch (Exception ex) {
        fail("Failed due to exception: " + ex.getMessage());
      }
    });

    for (String volatilityClass : volatilityClasses) {
      runnables.add(() -> {
        try (Connection conn =
                getConnectionBuilder().withIsolationLevel(IsolationLevel.READ_COMMITTED)
                .withAutoCommit(AutoCommit.ENABLED).connect();
              Statement stmt = conn.createStatement();) {
          ResultSet rs =  stmt.executeQuery("SELECT " + volatilityClass + "_plpgsql_func();");
          assertTrue(rs.next());
          int firstVal = rs.getInt(volatilityClass +"_plpgsql_func");
          assertTrue(rs.next());
          int secondVal = rs.getInt(volatilityClass + "_plpgsql_func");
          if (volatilityClass.equalsIgnoreCase("VOLATILE"))
            assertNotEquals(secondVal, firstVal);
          else
            assertEquals(secondVal, firstVal);
        } catch (Exception ex) {
          fail("Failed due to exception: " + ex.getMessage());
        }
      });
    }

    ExecutorService es = Executors.newFixedThreadPool(4);
    List<Future<?>> futures = new ArrayList<>();

    for (Runnable r : runnables) {
      futures.add(es.submit(r));
    }

    try {
      LOG.info("Waiting for all threads");
      for (Future<?> future : futures) {
        future.get(20, TimeUnit.SECONDS);
      }
    } catch (TimeoutException ex) {
      LOG.warn("Threads info:\n\n" + ThreadUtil.getAllThreadsInfo());
      fail("Waiting for threads timed out, this is unexpected!");
    }
  }

  /*
   * Lazy evaluation in Postgres is an optimization to save memory and possibly computation in cases
   * when an SQL function is supposed to return the results of the last statement (and it is a
   * SELECT) and the caller of the SQL function doesn't require all results from the function at
   * once.
   *
   * Consider the following example with 2 functions (an SQL function and a PLPGSQL function):
   *
   * CREATE OR REPLACE FUNCTION get_rows() RETURNS TABLE(z VARCHAR(100))
   *  ...
   *  ...
   *  SELECT * from t;
   *
   * CREATE OR REPLACE PROCEDURE outer_func() LANGUAGE PLPGSQL
   *  ...
   *  ...
   *  FOR target IN SELECT get_rows() LOOP
   *    ....
   *
   * In outer_func, there is a FOR LOOP that iterates over the results of the get_rows SQL function.
   * One way to perform this execution would be to fetch all records of the last SELECT statement in
   * get_rows() and store them in memory. Then, the FOR LOOP can perform each iteration on a new row
   * picked from memory. However, this requires using a possibly large amount of memory. Also, in
   * case the FOR LOOP was to exit early based on some condition, the rest of the rows stored in
   * memory would be rendered useless along with the computation that was done to fetch them in the
   * get_rows function. For this reason, Postgres allows SQL functions which have a SELECT statement
   * at the end to be lazily evaluable. This means that the function allows a caller to request rows
   * one at a time -- hence removing the need for the function to fetch all rows of that SELECT and
   * store in memory.
   *
   * Note that in READ COMMITTED isolation, each statement in a volatile function runs on a new
   * snapshot. Lazy evaluation requires executing a function's last SELECT statement partially
   * multiple times. But all rows from a lazily evaluated SELECT statement should always correspond
   * to a consistent snapshot. But, other execution in the backend between two calls to a lazily
   * evaluable function might require running on a newer snapshot. Postgres handles this by storing
   * snapshot information in the lazy function's execution state and switching back to that snapshot
   * when context moves back to the function to fetch more rows. The below code is where Postgres
   * switches back to a saved snapshot when re-entering a lazily evaluated function -
   *
   * /* Re-establish active snapshot when re-entering function ...
   * PushActiveSnapshot(es->qd->snapshot);
   *
   * In YB, a consistent read point is the analogue of a snapshot. The information about a
   * consistent read point is maintained in pg_client_session.cc on the tserver process of the same
   * node. Only one consistent read point can be stored for a transaction at a single point in time.
   * YSQL is allowed to change the consistent read point in limited ways: change it to the current
   * time as seen by the tserver process, set it to the restart read time it in case of a read
   * restart or update it to a specific value in case of backfill. But YSQL doesn't have the
   * capability to read the consistent read point that is chosen by the tserver process if set to
   * the current time. So, it is not possible to switch back to the consistent read point of a
   * lazily evaluated query.
   *
   * This limitation results in later invocations of a lazily evaluated query to possibly use later
   * consistent read points that were set as part of other execution between invocations to the
   * lazily evaluated query. This breaks correctness since the data now doesn't correspond to a
   * single snapshot.
   *
   * This limitation will be addressed in #12959. Till we add the necessary framework to allow
   * saving and reusing snapshots, YSQL disables lazy evaluation in READ COMMITTED isolation to
   * avoid correctness issues that can stem from not using a single snapshot for a lazily evaluated
   * query.
   */
  @Test
  public void testLazilyEvaluatedSQLFunctions() throws Exception {
    int numTablets = 10;
    int initialTableSize = 10; // number of rows

    try (Statement statement = connection.createStatement()) {
      statement.execute(
        "CREATE TABLE t(k SERIAL PRIMARY KEY, v VARCHAR(100) NOT NULL)" +
        " SPLIT INTO " + numTablets + " TABLETS");
      statement.execute(
        "CREATE TABLE t2(k SERIAL PRIMARY KEY, v VARCHAR(100) NOT NULL)");
      for (int i = 0; i < initialTableSize; i++) {
        statement.execute("INSERT INTO t(v) VALUES ('cat')");
      }

      // Create a lazily evaluable function i.e., the last statement should be
      // a SELECT.
      statement.execute(
        "CREATE OR REPLACE FUNCTION get_rows() RETURNS TABLE(z VARCHAR(100)) " +
        "LANGUAGE SQL " +
        " AS $$ " +
        "   SELECT v FROM t; " +
        " $$;");

      statement.execute(
        "CREATE OR REPLACE PROCEDURE outer_func() LANGUAGE PLPGSQL " +
        " AS $$ " +
        "   DECLARE " +
        "     target RECORD; " +
        "   BEGIN " +
        "     FOR target IN SELECT get_rows() LOOP " +
        "       INSERT INTO t2(v) VALUES (target); " +
        "       PERFORM pg_sleep(1); " +
        "     END LOOP; " +
        "   END; " +
        " $$;");
    }

    List<Runnable> runnables = new ArrayList<>();

    runnables.add(() -> {
      try (Connection conn = getConnectionBuilder()
              .withIsolationLevel(IsolationLevel.READ_COMMITTED)
              .withAutoCommit(AutoCommit.ENABLED).connect();
            Statement stmt = conn.createStatement();) {
        stmt.execute("SET yb_plpgsql_disable_prefetch_in_for_query=true");
        stmt.execute("CALL outer_func()");
      } catch (Exception ex) {
        fail("Failed due to exception: " + ex.getMessage());
      }
    });

    runnables.add(() -> {
      try (Connection conn = getConnectionBuilder()
              .withIsolationLevel(IsolationLevel.READ_COMMITTED)
              .withAutoCommit(AutoCommit.ENABLED).connect();
            Statement stmt = conn.createStatement();) {
        // This is to ensure that we start inserting more rows into table t once
        // we are sure that "CALL outer_func" in thread 1 has
        // reached the "FOR target IN SELECT get_rows() LOOP" line and picked
        // a snapshot to run the SELECT in get_rows(). So, we would expect only
        // initialTableSize number of iteration of the FOR loop and hence only
        // those many rows to be inserted into table t2.
        Thread.sleep(2000);
        for (int i = 0; i < numTablets; i++) {
          stmt.execute("INSERT INTO t(v) VALUES ('cat')");
        }
      } catch (Exception ex) {
        fail("Failed due to exception: " + ex.getMessage());
      }
    });

    ExecutorService es = Executors.newFixedThreadPool(2);
    List<Future<?>> futures = new ArrayList<>();

    for (Runnable r : runnables) {
      futures.add(es.submit(r));
    }

    try {
      LOG.info("Waiting for all threads");
      for (Future<?> future : futures) {
        future.get(20, TimeUnit.SECONDS);
      }
    } catch (TimeoutException ex) {
      LOG.warn("Threads info:\n\n" + ThreadUtil.getAllThreadsInfo());
      fail("Waiting for threads timed out, this is unexpected!");
    }

    try (Statement statement = connection.createStatement()) {
      assertQuery(statement, "SELECT COUNT(*) FROM t2",
                  new Row(initialTableSize));
    }
  }
}
