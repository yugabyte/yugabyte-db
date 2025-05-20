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

import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.YBTestRunner;

import java.sql.ResultSet;
import java.sql.Statement;
import java.util.Map;
import java.util.regex.Pattern;

import com.yugabyte.util.PSQLException;
import static org.yb.AssertionWrappers.*;

@RunWith(value=YBTestRunner.class)
public class TestPgTimeout extends BasePgSQLTest {
  private static final Logger LOG = LoggerFactory.getLogger(TestPgSelect.class);
  private static final int kSlowdownPgsqlAggregateReadMs = 2000;
  private static final int kTimeoutMs = 1000;
  private static final int kSleep = 5;

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("TEST_slowdown_pgsql_aggregate_read_ms",
        Integer.toString(kSlowdownPgsqlAggregateReadMs));
    return flagMap;
  }

  @Test
  public void testTimeout() throws Exception {
    Statement statement = connection.createStatement();
    setupSimpleTable("timeouttest");
    String query = "SELECT count(*) FROM timeouttest";
    long start;
    long stop;

    // The default case there is no statement timeout.
    boolean timeoutEncountered = false;
    start = System.currentTimeMillis();
    try (ResultSet rs = statement.executeQuery(query)) {
    } catch (PSQLException ex) {
      if (Pattern.matches(".*RPC .* timed out after.*", ex.getMessage())) {
        LOG.info("Timeout ERROR: " + ex.getMessage());
        timeoutEncountered = true;
      }
    }
    stop = System.currentTimeMillis();
    assertFalse(timeoutEncountered);
    assertGreaterThan((int) (stop - start), kSlowdownPgsqlAggregateReadMs);

    query = "SET STATEMENT_TIMEOUT=" + kTimeoutMs;
    statement.execute(query);

    // We also adjust RPC timeout to the statement timeout when statement timeout is shorter than
    // default RPC timeout. We will see RPC timed out in the error message.
    query = "SELECT count(*) FROM timeouttest";
    start = System.currentTimeMillis();
    try (ResultSet rs = statement.executeQuery(query)) {
    } catch (PSQLException ex) {
      if (ex.getMessage().contains("canceling statement due to statement timeout")) {
        LOG.info("Timeout ERROR: " + ex.getMessage());
        timeoutEncountered = true;
      }
    }
    stop = System.currentTimeMillis();
    assertTrue(timeoutEncountered);
    assertLessThan((int) (stop - start), kSlowdownPgsqlAggregateReadMs);

    timeoutEncountered = false;
    query = "SELECT pg_sleep(" + kSleep + ") FROM timeouttest";
    start = System.currentTimeMillis();
    try (ResultSet rs = statement.executeQuery(query)) {
    } catch (PSQLException ex) {
      if (ex.getMessage().contains("canceling statement due to statement timeout")) {
        LOG.info("Timeout ERROR: " + ex.getMessage());
        timeoutEncountered = true;
      }
    }
    stop = System.currentTimeMillis();
    assertTrue(timeoutEncountered);
    assertLessThan((int) (stop - start), kSleep * 1000);

    if (isTestRunningWithConnectionManager()) {
      query = "SET STATEMENT_TIMEOUT=0";
      statement.execute(query);
    }
    LOG.info("Done with the test");
  }

  private void cancel(Statement stmt, long wait) {
    try {
      Thread.sleep(wait);
      stmt.cancel();
    } catch (Exception ex) {
      LOG.error("Error while attempting to cancel statement", ex);
    }
  }

  @Test
  public void testCancel() throws Exception {
    Statement statement = connection.createStatement();
    setupSimpleTable("canceltest");
    String query = "SELECT count(*) FROM canceltest";
    long start;
    long stop;

    // The default case there is no cancel.
    boolean timeoutEncountered = false;
    start = System.currentTimeMillis();
    try (ResultSet rs = statement.executeQuery(query)) {
    } catch (PSQLException ex) {
      if (Pattern.matches(".*RPC .* timed out after.*", ex.getMessage())) {
        LOG.info("Timeout ERROR: " + ex.getMessage());
        timeoutEncountered = true;
      }
    }
    stop = System.currentTimeMillis();
    assertFalse(timeoutEncountered);
    assertGreaterThan((int) (stop - start), kSlowdownPgsqlAggregateReadMs);
    Thread cancelThread = new Thread(() -> {
      cancel(statement, kTimeoutMs);
    });
    cancelThread.start();
    start = System.currentTimeMillis();
    try (ResultSet rs = statement.executeQuery(query)) {
    } catch (PSQLException ex) {
      if (ex.getMessage().contains("canceling statement due to user request")) {
        LOG.info("Timeout ERROR: " + ex.getMessage());
        timeoutEncountered = true;
      } else {
        LOG.info("Other ERROR: " + ex.getMessage());
      }
    }
    stop = System.currentTimeMillis();
    cancelThread.join();
    assertTrue(timeoutEncountered);
    assertLessThan((int) (stop - start), kSlowdownPgsqlAggregateReadMs);

    timeoutEncountered = false;
    query = "SELECT pg_sleep(" + kSleep + ") FROM canceltest";
    cancelThread = new Thread(() -> {
      cancel(statement, kTimeoutMs);
    });
    cancelThread.start();
    start = System.currentTimeMillis();
    try (ResultSet rs = statement.executeQuery(query)) {
    } catch (PSQLException ex) {
      if (ex.getMessage().contains("canceling statement due to user request")) {
        LOG.info("Timeout ERROR: " + ex.getMessage());
        timeoutEncountered = true;
      }
    }
    stop = System.currentTimeMillis();
    cancelThread.join();
    assertTrue(timeoutEncountered);
    assertLessThan((int) (stop - start), kSleep * 1000);

    LOG.info("Done with the test");
  }
}
