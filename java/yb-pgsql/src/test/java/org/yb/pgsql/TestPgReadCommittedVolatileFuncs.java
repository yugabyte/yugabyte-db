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
import org.yb.util.YBTestRunnerNonTsanOnly;

/**
 *
 * Test to ensure that each statement in a volatile plpgsql function uses a
 * new snapshot and all statements in immutable and stable functions use the
 * same snapshot.
 *
 * Quoting from Pg docs (https://www.postgresql.org/docs/current/xfunc-volatility.html):
 *   "STABLE and IMMUTABLE functions use a snapshot established as of the start of the calling
 *    query, whereas VOLATILE functions obtain a fresh snapshot at the start of each
 *    query they execute."
 */
@RunWith(value = YBTestRunnerNonTsanOnly.class)
public class TestPgReadCommittedVolatileFuncs extends BasePgSQLTest {
  private static final Logger LOG =
    LoggerFactory.getLogger(TestPgReadCommittedVolatileFuncs.class);

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

  @Test
  public void testFunctionSemantics() throws Exception {
    String[] volatilityClasses = {"VOLATILE", "IMMUTABLE", "STABLE"};
    try (Statement statement = connection.createStatement()) {
      statement.execute("CREATE TABLE TEST (k INT PRIMARY KEY, v INT)");
      statement.execute("INSERT INTO TEST VALUES (1, 0)");
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

    ExecutorService es = Executors.newFixedThreadPool(4);
    List<Future<?>> futures = new ArrayList<>();
    List<Runnable> runnables = new ArrayList<>();

    runnables.add(() -> {
      try (Connection conn =
              getConnectionBuilder().withIsolationLevel(IsolationLevel.READ_COMMITTED)
              .withAutoCommit(AutoCommit.ENABLED).connect();
            Statement stmt = conn.createStatement();) {
        stmt.execute("CALL update_row();");
      }
      catch (Exception ex) {
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
            assertTrue(secondVal != firstVal);
          else
            assertTrue(secondVal == firstVal);
        }
        catch (Exception ex) {
          fail("Failed due to exception: " + ex.getMessage());
        }
      });
    }

    for (Runnable r : runnables) {
      futures.add(es.submit(r));
    }

    try {
      LOG.info("Waiting for all threads");
      for (Future<?> future : futures) {
        future.get(10, TimeUnit.SECONDS);
      }
    } catch (TimeoutException ex) {
      LOG.warn("Threads info:\n\n" + ThreadUtil.getAllThreadsInfo());
      fail("Waiting for threads timed out, this is unexpected!");
    }

    try (Statement statement = connection.createStatement()) {
      statement.execute("DROP TABLE test");
      statement.execute("DROP PROCEDURE update_row");
      for (String volatilityClass : volatilityClasses) {
        statement.execute("DROP FUNCTION " + volatilityClass + "_plpgsql_func");
      }
    }
  }
}
