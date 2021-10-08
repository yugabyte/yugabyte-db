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

import java.util.Arrays;
import java.util.List;
import java.util.Map;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.util.YBTestRunnerNonTsanOnly;

import java.sql.Connection;
import java.sql.Statement;

import static org.yb.AssertionWrappers.*;
import com.google.common.collect.ImmutableMap;

/**
 * Runs the pg_regress test suite on YB code.
 */
@RunWith(value=YBTestRunnerNonTsanOnly.class)
public class TestPgRegressTablespaces extends BasePgSQLTest {

  private List<Map<String, String>> perTserverZonePlacementFlags = Arrays.asList(
      ImmutableMap.of(
          "placement_cloud", "cloud1",
          "placement_region", "region1",
          "placement_zone", "zone1"),
      ImmutableMap.of(
          "placement_cloud", "cloud2",
          "placement_region", "region2",
          "placement_zone", "zone2"),
      ImmutableMap.of(
          "placement_cloud", "cloud1",
          "placement_region", "region1",
          "placement_zone", "zone2"),
      ImmutableMap.of(
        "placement_cloud", "cloud1",
        "placement_region", "region2",
        "placement_zone", "zone1"));

  private Map<String, String> masterFlags =
    ImmutableMap.of(
        "placement_cloud", "cloud1",
        "placement_region", "region1",
        "placement_zone", "zone1");

  @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    builder.enablePgTransactions(true);
    builder.perTServerFlags(perTserverZonePlacementFlags);
    builder.numTservers(perTserverZonePlacementFlags.size());
    builder.addMasterFlags(masterFlags);
  }


  @Override
  public int getTestMethodTimeoutSec() {
    return 5000;
  }

  @Test
  public void testPgRegressTablespaces() throws Exception {
    runPgRegressTest("yb_tablespaces_schedule");
  }

  @Test
  public void testYBLocalTableViews() throws Exception {
    // Create a view using the yb_is_local_table function. Selecting rows from the
    // view must give different results depending on the location of the node we
    // are connecting to, to execute the query.

    // Test setup.
    try (Connection connection1 = getConnectionBuilder().withTServer(0).connect();
         Statement statement1 = connection1.createStatement()) {
      // Create tablespaces.
      statement1.execute("CREATE TABLESPACE localtablespace " +
          "  WITH (replica_placement=" +
          "'{\"num_replicas\":1, \"placement_blocks\":" +
          "[{\"cloud\":\"cloud1\",\"region\":\"region1\",\"zone\":\"zone1\"," +
          "\"min_num_replicas\":1}]}')");

      statement1.execute("CREATE TABLESPACE remotetablespace " +
          "  WITH (replica_placement=" +
          "'{\"num_replicas\":1, \"placement_blocks\":" +
          "[{\"cloud\":\"cloud2\",\"region\":\"region2\",\"zone\":\"zone2\"," +
          "\"min_num_replicas\":1}]}')");

      // Create a partitioned table with one local and one remote partition.
      statement1.executeUpdate("CREATE TABLE prt (a int, b varchar) PARTITION BY RANGE(a)");
      statement1.executeUpdate("CREATE TABLE prt_p1 PARTITION OF prt FOR VALUES FROM (0) TO (10) " +
                               "TABLESPACE localtablespace");
      statement1.executeUpdate("CREATE TABLE prt_p2 PARTITION OF prt FOR VALUES FROM (10) TO (20)" +
                               " TABLESPACE remotetablespace");
      // Insert values into the partitioned table, with 2 rows in the local partition
      // and 1 row in the remote.
      for (int i = 0; i <= 10; i += 5) {
        statement1.executeUpdate(String.format("INSERT INTO prt VALUES (%d, 'abc')", i));
      }

      // Create a VIEW with the function yb_is_local_table.
      statement1.executeUpdate("CREATE VIEW localpartition AS SELECT * FROM prt WHERE " +
                               "yb_is_local_table(prt.tableoid)");
      // Select from the VIEW. Since we are querying from the local node, verify that
      // we indeed got back 2 rows.
      assertEquals(getRowList(statement1, "SELECT * FROM localpartition").size(), 2);
    }

    // Query the VIEW from the remote node. Verify that only one row is returned.
    try (Connection connection2 = getConnectionBuilder().withTServer(1).connect();
         Statement statement2 = connection2.createStatement()) {
      assertEquals(getRowList(statement2, "SELECT * FROM localpartition").size(), 1);
    }
  }
}
