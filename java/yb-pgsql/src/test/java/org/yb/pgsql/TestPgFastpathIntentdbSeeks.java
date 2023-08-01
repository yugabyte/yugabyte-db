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
import org.yb.YBTestRunner;

import java.sql.Connection;
import java.sql.Statement;
import java.util.Map;

import static org.yb.AssertionWrappers.*;

@RunWith(YBTestRunner.class)
public class TestPgFastpathIntentdbSeeks extends BasePgSQLTestWithRpcMetric {
  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("enable_wait_queues", "false");
    flagMap.put("enable_deadlock_detection", "false");
    flagMap.put("ysql_max_write_restart_attempts", Integer.toString(0));
    // Verbose logging of intentsdb seeks/postgres statements
    flagMap.put("vmodule", "docdb=4,conflict_resolution=5");
    flagMap.put("ysql_log_statement", "all");
    return flagMap;
  }

  @Test
  public void testFastpathIntentdbSeeks() throws Exception {
    try (Connection extraConnection = getConnectionBuilder().connect();
         Statement stmt = connection.createStatement();
         Statement extraStmt = extraConnection.createStatement()) {
      stmt.execute("SET yb_transaction_priority_lower_bound = 1");
      stmt.execute("CREATE TABLE t(k1 int, k2 int, r1 int, r2 int, v1 int, v2 int, v3 int, v4 int, "
        + "PRIMARY KEY((k1,k2)HASH, r1, r2))");
      stmt.execute("INSERT INTO t VALUES (1, 1, 1, 1, 1, 1, 1, 1)");
      stmt.execute("BEGIN TRANSACTION ISOLATION LEVEL READ COMMITTED");
      stmt.execute("UPDATE t SET v1 = 2, v2 = 2, v3 = 2, v4 = 2 WHERE k1 = 1 AND k2 = 1 AND r1 = 1 "
        + "AND r2 = 1");
      OperationsCounter counter = updateCounter(new OperationsCounter("t"));

      // This query will trigger a single tablet operation (fastpath), which will seek in the
      // intentsdb for transaction conflicts. It will go through conflict resulution, performing
      // one seek per doc key component.
      runInvalidQuery(extraStmt, "UPDATE t SET v1 = 3, v2 = 3, v3 = 3, v4 = 3 WHERE k1 = 1 AND "
          + "k2 = 1 AND r1 = 1 AND r2 = 1", true,
        "could not serialize access due to concurrent update",
        "conflicts with higher priority transaction");
      updateCounter(counter);
      final int seeks = counter.intentdbSeeks.get("t").value();

      // - Expect one seek per conflict resolution.
      assertEquals(seeks, 1);
      stmt.execute("COMMIT");
    }
  }
}
