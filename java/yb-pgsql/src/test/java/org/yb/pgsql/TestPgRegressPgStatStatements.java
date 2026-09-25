package org.yb.pgsql;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.client.TestUtils;
import org.yb.YBTestRunner;

import java.io.File;
import java.sql.Statement;

import java.util.Map;

@RunWith(value=YBTestRunner.class)
public class TestPgRegressPgStatStatements extends BasePgRegressTest {
  @Override
  public int getTestMethodTimeoutSec() {
    return 1800;
  }

  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    // Disable auto analyze because ANALYZE-related statements recorded in pg_stat_statements make
    // the test flaky.
    flagMap.put("ysql_enable_auto_analyze", "false");
    // yb_enable_read_committed_isolation defaults to true only in release builds, which makes
    // yb.port.utility's SET TRANSACTION ISOLATION LEVEL READ COMMITTED emit a "read committed
    // isolation is disabled" warning on every other build type. Pin it on so the ported test
    // exercises the isolation level that ships.
    flagMap.put("yb_enable_read_committed_isolation", "true");
    return flagMap;
  }

  @Test
  @BypassConnMgr(reason = BasePgSQLTest.GUC_REPLAY_AFFECTS_QUERIES_EXEC_RESULT)
  public void schedule() throws Exception {
    runPgRegressTest(new File(TestUtils.getBuildRootDir(),
                              "postgres_build/contrib/pg_stat_statements"),
                     "yb_schedule");
  }
}
