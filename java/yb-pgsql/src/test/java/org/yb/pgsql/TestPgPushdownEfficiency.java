package org.yb.pgsql;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.YBTestRunner;

import java.sql.Statement;
import java.util.Map;

import static org.yb.AssertionWrappers.assertEquals;

@RunWith(YBTestRunner.class)
public class TestPgPushdownEfficiency extends BasePgSQLTestWithRpcMetric {
  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("enable_object_lock_fastpath", "false");
    flagMap.put("pg_client_use_shared_memory", "false");
    return flagMap;
  }

  @Test
  public void testSelect() throws Exception {
    try (Statement stmt = connection.createStatement()) {
      stmt.execute(
        "CREATE TABLE t(h INT, r1 INT, r2 INT, r3 INT, PRIMARY KEY(h, r1 ASC, r2 ASC, r3 ASC))");
      stmt.execute("SELECT * FROM t");
      stmt.execute("INSERT INTO t SELECT 1, s % 10, s, s FROM generate_series(1, 100000) AS s");
      OperationsCounter counter = updateCounter(new OperationsCounter());
      final int queryCount = 1000;
      for (int i = 0; i < queryCount; ++i) {
        stmt.execute("SELECT * FROM t WHERE h = 1 AND r1 = 5 AND r3 = 50005");
        getSingleRow(stmt.getResultSet());
      }
      updateCounter(counter);
      assertEquals(2, counter.rpc.value() / queryCount);
    }
  }
}
