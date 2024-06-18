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

import com.google.common.collect.ImmutableMap;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.util.YBTestRunnerNonTsanOnly;

/**
 * Runs the pg_regress test suite on YB code.
 */
@RunWith(value = YBTestRunnerNonTsanOnly.class)
public class TestPgRegressColocatedTablesWithTablespaces extends BasePgRegressTest {
  private List<Map<String, String>> perTserverZonePlacementFlags =
      Arrays.asList(ImmutableMap.of("placement_cloud", "cloud1", "placement_region", "region1",
                        "placement_zone", "zone1"),
          ImmutableMap.of("placement_cloud", "cloud1", "placement_region", "region1",
              "placement_zone", "zone2"),
          ImmutableMap.of("placement_cloud", "cloud1", "placement_region", "region1",
              "placement_zone", "zone3"),
          ImmutableMap.of("placement_cloud", "cloud1", "placement_region", "region2",
              "placement_zone", "zone1"),
          ImmutableMap.of("placement_cloud", "cloud1", "placement_region", "region2",
              "placement_zone", "zone2"),
          ImmutableMap.of("placement_cloud", "cloud1", "placement_region", "region2",
              "placement_zone", "zone3"),
          ImmutableMap.of("placement_cloud", "cloud1", "placement_region", "region3",
              "placement_zone", "zone1"),
          ImmutableMap.of("placement_cloud", "cloud1", "placement_region", "region3",
              "placement_zone", "zone2"),
          ImmutableMap.of("placement_cloud", "cloud1", "placement_region", "region3",
              "placement_zone", "zone3"));

  private Map<String, String> masterFlags = ImmutableMap.of(
      "placement_cloud", "cloud1", "placement_region", "region1", "placement_zone", "zone1");

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    builder.enablePgTransactions(true);
    builder.perTServerFlags(perTserverZonePlacementFlags);
    builder.numTservers(perTserverZonePlacementFlags.size());
    builder.addMasterFlags(masterFlags);
  }

  @Override
  protected Map<String, String> getTServerFlags() {
    Map<String, String> flagMap = super.getTServerFlags();
    if (isTestRunningWithConnectionManager()) {
      flagMap.put("allowed_preview_flags_csv",
            "ysql_enable_colocated_tables_with_tablespaces,enable_ysql_conn_mgr");
    }
    else {
      flagMap.put("allowed_preview_flags_csv", "ysql_enable_colocated_tables_with_tablespaces");
    }
    flagMap.put("ysql_enable_colocated_tables_with_tablespaces", "true");
    return flagMap;
  }

  @Override
  protected Map<String, String> getMasterFlags() {
    Map<String, String> flagMap = super.getMasterFlags();
    flagMap.put("allowed_preview_flags_csv", "ysql_enable_colocated_tables_with_tablespaces");
    flagMap.put("ysql_enable_colocated_tables_with_tablespaces", "true");
    return flagMap;
  }

  @Override
  public int getTestMethodTimeoutSec() {
    return 5000;
  }

  @Test
  public void testPgRegressColocatedTablesWithTablespaces() throws Exception {
    runPgRegressTest("yb_colocated_tables_with_tablespaces_schedule");
  }
}
