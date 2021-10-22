package org.yb.pgsql;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.client.TestUtils;
import org.yb.util.YBTestRunnerNonTsanOnly;

import java.io.File;
import java.sql.Statement;

@RunWith(value=YBTestRunnerNonTsanOnly.class)
public class TestPgRegressPgStatMonitor extends BasePgSQLTest {
  @Override
  public int getTestMethodTimeoutSec() {
    return 1800;
  }

  @Test
  public void schedule() throws Exception {
    runPgRegressTest(new File(TestUtils.getBuildRootDir(),
                              "postgres_build/contrib/pg_stat_monitor"),
                     "yb_schedule");
  }
}
