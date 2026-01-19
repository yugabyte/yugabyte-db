// Copyright (c) YugabyteDB, Inc.
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
import org.yb.client.TestUtils;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.YBTestRunner;

import java.io.File;
import java.util.Collections;
import java.util.Map;

import static org.yb.AssertionWrappers.*;

@RunWith(value=YBTestRunner.class)
public class TestPgRegressThirdPartyExtensionsHypopg extends BasePgRegressTest {
  @Override
  public int getTestMethodTimeoutSec() {
    return 1800;
  }

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    builder.addCommonTServerFlag("TEST_ysql_conn_mgr_dowarmup_all_pools_mode", "none");
  }

  @Test
  public void schedule_old_cost_model() throws Exception {
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("ysql_pg_conf_csv", "yb_enable_cbo=OFF");
    restartClusterWithFlags(Collections.emptyMap(), flagMap);

    String enable_auto_analyze = miniCluster.getClient().getFlag(
        miniCluster.getTabletServers().keySet().iterator().next(),
        "ysql_enable_auto_analyze");
    assertTrue("ysql_enable_auto_analyze should remain at default value false",
        enable_auto_analyze.equals("false"));

    runPgRegressTest(new File(TestUtils.getBuildRootDir(),
                              "postgres_build/third-party-extensions/hypopg"),
                     "yb_old_cost_model_schedule");
  }

  @Test
  public void schedule() throws Exception {
    Map<String, String> flagMap = super.getTServerFlags();
    flagMap.put("ysql_pg_conf_csv", "yb_enable_cbo=ON");
    restartClusterWithFlags(Collections.emptyMap(), flagMap);

    String enable_auto_analyze = miniCluster.getClient().getFlag(
        miniCluster.getTabletServers().keySet().iterator().next(),
        "ysql_enable_auto_analyze");
    assertTrue("ysql_enable_auto_analyze should be auto set to true",
        enable_auto_analyze.equals("true"));

    runPgRegressTest(new File(TestUtils.getBuildRootDir(),
                              "postgres_build/third-party-extensions/hypopg"),
                     "yb_schedule");
  }
}
