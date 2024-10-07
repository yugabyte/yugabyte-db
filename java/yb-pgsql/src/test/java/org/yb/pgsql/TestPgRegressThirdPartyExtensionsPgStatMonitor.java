package org.yb.pgsql;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.client.TestUtils;
import org.yb.util.YBTestRunnerNonTsanOnly;

import java.io.File;
import java.sql.Statement;

import java.util.Map;

@RunWith(value=YBTestRunnerNonTsanOnly.class)
public class TestPgRegressThirdPartyExtensionsPgStatMonitor extends BasePgRegressTest {
  @Override
  public int getTestMethodTimeoutSec() {
    return 1800;
  }

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    appendToYsqlPgConf(flagMap, "shared_preload_libraries='pg_stat_monitor'");
    return flagMap;
  }

  @Test
  public void schedule() throws Exception {
    runPgRegressTest(new File(TestUtils.getBuildRootDir(),
                              "postgres_build/third-party-extensions/pg_stat_monitor"),
                     "yb_schedule");
  }
}
