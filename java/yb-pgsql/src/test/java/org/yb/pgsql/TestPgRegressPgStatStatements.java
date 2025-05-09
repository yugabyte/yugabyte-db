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

  @Test
  public void schedule() throws Exception {
    skipYsqlConnMgr(BasePgSQLTest.GUC_REPLAY_AFFECTS_QUERIES_EXEC_RESULT,
                isTestRunningWithConnectionManager());
    runPgRegressTest(new File(TestUtils.getBuildRootDir(),
                              "postgres_build/contrib/pg_stat_statements"),
                     "yb_schedule");
  }
}
