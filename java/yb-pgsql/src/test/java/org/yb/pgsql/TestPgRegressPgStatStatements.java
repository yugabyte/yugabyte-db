package org.yb.pgsql;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.client.TestUtils;
import org.yb.YBTestRunner;

import java.io.File;
import java.sql.Statement;

import java.util.Map;

@RunWith(value=YBTestRunner.class)
public class TestPgRegressPgStatStatements extends BasePgSQLTest {
  @Override
  public int getTestMethodTimeoutSec() {
    return 1800;
  }

  @Test
  public void schedule() throws Exception {
    runPgRegressTest(new File(TestUtils.getBuildRootDir(),
                              "postgres_build/contrib/pg_stat_statements"),
                     "yb_schedule");
  }
}
