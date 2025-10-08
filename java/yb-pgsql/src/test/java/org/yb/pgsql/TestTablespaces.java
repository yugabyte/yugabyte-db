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

import java.util.Arrays;
import java.util.List;
import java.util.Map;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.util.YBTestRunnerNonTsanOnly;

import java.sql.Connection;
import java.sql.Statement;

import org.yb.util.json.Checkers;
import org.yb.util.json.JsonUtil;
import org.yb.pgsql.ExplainAnalyzeUtils.PlanCheckerBuilder;
import org.yb.pgsql.ExplainAnalyzeUtils.TopLevelCheckerBuilder;

import static org.yb.AssertionWrappers.*;
import com.google.common.collect.ImmutableMap;

import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_INDEX_ONLY_SCAN;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_INDEX_SCAN;

@RunWith(value=YBTestRunnerNonTsanOnly.class)
public class TestTablespaces extends BasePgSQLTest {
  private List<Map<String, String>> perTserverZonePlacementFlags = Arrays.asList(
      ImmutableMap.of(
          "placement_cloud", "cloud1",
          "placement_region", "region1",
          "placement_zone", "zone1"),
      ImmutableMap.of(
          "placement_cloud", "cloud1",
          "placement_region", "region1",
          "placement_zone", "zone2"),
      ImmutableMap.of(
          "placement_cloud", "cloud1",
          "placement_region", "region2",
          "placement_zone", "zone1"),
      ImmutableMap.of(
          "placement_cloud", "cloud2",
          "placement_region", "region1",
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

  private static TopLevelCheckerBuilder makeTopLevelBuilder() {
    return JsonUtil.makeCheckerBuilder(TopLevelCheckerBuilder.class, false /* nullify */);
  }

  private static PlanCheckerBuilder makePlanBuilder() {
    return JsonUtil.makeCheckerBuilder(PlanCheckerBuilder.class, false /* nullify */);
  }

  private void testYBTablespaceLocalIndexHelper
    (Boolean enable_base_scans_cost_model) throws Exception {
  try (Connection connection1 = getConnectionBuilder().withTServer(0).connect();
      Statement statement1 = connection1.createStatement()) {

    assertOneRow(statement1, "SELECT yb_server_cloud()", "cloud1");
    assertOneRow(statement1, "SELECT yb_server_region()", "region1");
    assertOneRow(statement1, "SELECT yb_server_zone()", "zone1");

    if (enable_base_scans_cost_model)
    {
      statement1.execute("SET yb_enable_base_scans_cost_model = true;");
      statement1.execute("ANALYZE foo;");
    }
    else
      statement1.execute("SET yb_enable_base_scans_cost_model = false;");

    ExplainAnalyzeUtils.testExplain(
      statement1,
      "SELECT * FROM foo WHERE x = 5",
      makeTopLevelBuilder()
        .storageReadRequests(Checkers.greater(0))
        .storageWriteRequests(Checkers.equal(0))
        .storageExecutionTime(Checkers.greaterOrEqual(0.0))
        .plan(makePlanBuilder()
          .nodeType(NODE_INDEX_ONLY_SCAN)
          .indexName("c1r1z1_index_x")
          .build())
        .build());
  }

  try (Connection connection1 = getConnectionBuilder().withTServer(1).connect();
      Statement statement1 = connection1.createStatement()) {

    assertOneRow(statement1, "SELECT yb_server_cloud()", "cloud1");
    assertOneRow(statement1, "SELECT yb_server_region()", "region1");
    assertOneRow(statement1, "SELECT yb_server_zone()", "zone2");

    if (enable_base_scans_cost_model)
      statement1.execute("SET yb_enable_base_scans_cost_model = true;");
    else
      statement1.execute("SET yb_enable_base_scans_cost_model = false;");

    ExplainAnalyzeUtils.testExplain(
      statement1,
      "SELECT * FROM foo WHERE x = 5",
      makeTopLevelBuilder()
        .storageReadRequests(Checkers.greater(0))
        .storageWriteRequests(Checkers.equal(0))
        .storageExecutionTime(Checkers.greaterOrEqual(0.0))
        .plan(makePlanBuilder()
          .nodeType(NODE_INDEX_ONLY_SCAN)
          .indexName("c1r1z2_index_x")
          .build())
        .build());
  }

  try (Connection connection1 = getConnectionBuilder().withTServer(2).connect();
      Statement statement1 = connection1.createStatement()) {

    assertOneRow(statement1, "SELECT yb_server_cloud()", "cloud1");
    assertOneRow(statement1, "SELECT yb_server_region()", "region2");
    assertOneRow(statement1, "SELECT yb_server_zone()", "zone1");

    if (enable_base_scans_cost_model)
      statement1.execute("SET yb_enable_base_scans_cost_model = true;");
    else
      statement1.execute("SET yb_enable_base_scans_cost_model = false;");

    ExplainAnalyzeUtils.testExplain(
      statement1,
      "SELECT * FROM foo WHERE x = 5",
      makeTopLevelBuilder()
        .storageReadRequests(Checkers.greater(0))
        .storageWriteRequests(Checkers.equal(0))
        .storageExecutionTime(Checkers.greaterOrEqual(0.0))
        .plan(makePlanBuilder()
          .nodeType(NODE_INDEX_ONLY_SCAN)
          .indexName("c1r2z1_index_x")
          .build())
        .build());
  }

  // Try connecting to a different node, which should change the chosen index.
  try (Connection connection1 = getConnectionBuilder().withTServer(3).connect();
    Statement statement1 = connection1.createStatement()) {

    assertOneRow(statement1, "SELECT yb_server_cloud()", "cloud2");
    assertOneRow(statement1, "SELECT yb_server_region()", "region1");
    assertOneRow(statement1, "SELECT yb_server_zone()", "zone1");

    if (enable_base_scans_cost_model)
      statement1.execute("SET yb_enable_base_scans_cost_model = true;");
    else
      statement1.execute("SET yb_enable_base_scans_cost_model = false;");

    ExplainAnalyzeUtils.testExplain(
      statement1,
      "SELECT * FROM foo WHERE x = 5",
      makeTopLevelBuilder()
        .storageReadRequests(Checkers.greater(0))
        .storageWriteRequests(Checkers.equal(0))
        .storageExecutionTime(Checkers.greaterOrEqual(0.0))
        .plan(makePlanBuilder()
          .nodeType(NODE_INDEX_ONLY_SCAN)
          .indexName("c2r1z1_index_x")
          .build())
        .build());
  }
  }

  @Test
  public void testYBTablespaceLocalIndex() throws Exception {
    // Test that leader preference is a cost component when calculating
    // the cost of identical indexes on different tablespaces. In particular,
    // this test checks that connecting to different nodes will result in
    // different indexes being chosen; it's otherwise very basic. More
    // tests for leader preference can be found in the postgres regression
    // tests for tablespaces.

    try (Connection connection1 = getConnectionBuilder().withTServer(0).connect();
        Statement statement1 = connection1.createStatement()) {
      // Create tablespaces, with the same placements but different leader preferences.
      statement1.execute("CREATE TABLESPACE c1r1z1 WITH (replica_placement=" +
          "'{\"num_replicas\":1, \"placement_blocks\":" +
          "[{\"cloud\":\"cloud1\",\"region\":\"region1\",\"zone\":\"zone1\"," +
          "\"min_num_replicas\":1}]}')");

      statement1.execute("CREATE TABLESPACE c1r1z2 WITH (replica_placement=" +
          "'{\"num_replicas\":1, \"placement_blocks\":" +
          "[{\"cloud\":\"cloud1\",\"region\":\"region1\",\"zone\":\"zone2\"," +
          "\"min_num_replicas\":1}]}')");

      statement1.execute("CREATE TABLESPACE c1r2z1 WITH (replica_placement=" +
          "'{\"num_replicas\":1, \"placement_blocks\":" +
          "[{\"cloud\":\"cloud1\",\"region\":\"region2\",\"zone\":\"zone1\"," +
          "\"min_num_replicas\":1}]}')");

      statement1.execute("CREATE TABLESPACE c2r1z1 WITH (replica_placement=" +
          "'{\"num_replicas\":1, \"placement_blocks\":" +
          "[{\"cloud\":\"cloud2\",\"region\":\"region1\",\"zone\":\"zone1\"," +
          "\"min_num_replicas\":1}]}')");

      statement1.executeUpdate("CREATE TABLE foo(x int, y int)");
      statement1.executeUpdate("INSERT INTO foo (SELECT s, s FROM generate_series(1, 1000) s)");
      // Create identical indexes, one in each tablespace.
      statement1.executeUpdate("CREATE UNIQUE INDEX c1r1z1_index_x ON foo(x) INCLUDE (y) "
          + "TABLESPACE c1r1z1");
      statement1.executeUpdate("CREATE UNIQUE INDEX c1r1z2_index_x ON foo(x) INCLUDE (y) "
          + "TABLESPACE c1r1z2");
      statement1.executeUpdate("CREATE UNIQUE INDEX c1r2z1_index_x ON foo(x) INCLUDE (y) "
          + "TABLESPACE c1r2z1");
      statement1.executeUpdate("CREATE UNIQUE INDEX c2r1z1_index_x ON foo(x) INCLUDE (y) "
          + "TABLESPACE c2r1z1");
    }

    // Run without the cost model first. Table will be analyzed before running with cost model.
    testYBTablespaceLocalIndexHelper(false /* enable_base_scans_cost_model */);
    testYBTablespaceLocalIndexHelper(true /* enable_base_scans_cost_model */);
  }

  private void testYBTablespaceLocalExpressionIndexHelper
    (Boolean enable_base_scans_cost_model) throws Exception {
    try (Connection connection1 = getConnectionBuilder().withTServer(0).connect();
        Statement statement1 = connection1.createStatement()) {

      assertOneRow(statement1, "SELECT yb_server_cloud()", "cloud1");
      assertOneRow(statement1, "SELECT yb_server_region()", "region1");
      assertOneRow(statement1, "SELECT yb_server_zone()", "zone1");

      if (enable_base_scans_cost_model)
      {
        statement1.execute("SET yb_enable_base_scans_cost_model = true;");
        statement1.execute("ANALYZE foo;");
      }
      else
        statement1.execute("SET yb_enable_base_scans_cost_model = false;");

      ExplainAnalyzeUtils.testExplain(
        statement1,
        "SELECT x, y FROM foo WHERE x+y = 10",
        makeTopLevelBuilder()
          .storageReadRequests(Checkers.greater(0))
          .storageWriteRequests(Checkers.equal(0))
          .storageExecutionTime(Checkers.greaterOrEqual(0.0))
          .plan(makePlanBuilder()
            .nodeType(NODE_INDEX_ONLY_SCAN)
            .indexName("c1r1z1_index_x_plus_y")
            .build())
          .build());

      // Old cost model supports geolocation costing for index only scans only, but new cost model
      // should pick closest index for index scan too.
      if (enable_base_scans_cost_model) {
        ExplainAnalyzeUtils.testExplain(
        statement1,
        "SELECT * FROM foo WHERE x+y = 10",
        makeTopLevelBuilder()
          .storageReadRequests(Checkers.greater(0))
          .storageWriteRequests(Checkers.equal(0))
          .storageExecutionTime(Checkers.greaterOrEqual(0.0))
          .plan(makePlanBuilder()
            .nodeType(NODE_INDEX_SCAN)
            .indexName("c1r1z1_index_x_plus_y")
            .build())
          .build());
      }
    }

    try (Connection connection1 = getConnectionBuilder().withTServer(1).connect();
        Statement statement1 = connection1.createStatement()) {

      assertOneRow(statement1, "SELECT yb_server_cloud()", "cloud1");
      assertOneRow(statement1, "SELECT yb_server_region()", "region1");
      assertOneRow(statement1, "SELECT yb_server_zone()", "zone2");

      if (enable_base_scans_cost_model)
        statement1.execute("SET yb_enable_base_scans_cost_model = true;");
      else
        statement1.execute("SET yb_enable_base_scans_cost_model = false;");

      ExplainAnalyzeUtils.testExplain(
        statement1,
        "SELECT x, y FROM foo WHERE x+y = 10",
        makeTopLevelBuilder()
          .storageReadRequests(Checkers.greater(0))
          .storageWriteRequests(Checkers.equal(0))
          .storageExecutionTime(Checkers.greaterOrEqual(0.0))
          .plan(makePlanBuilder()
            .nodeType(NODE_INDEX_ONLY_SCAN)
            .indexName("c1r1z2_index_x_plus_y")
            .build())
          .build());

      // Old cost model supports geolocation costing for index only scans only, but new cost model
      // should pick closest index for index scan too.
      if (enable_base_scans_cost_model) {
        ExplainAnalyzeUtils.testExplain(
        statement1,
        "SELECT * FROM foo WHERE x+y = 10",
        makeTopLevelBuilder()
          .storageReadRequests(Checkers.greater(0))
          .storageWriteRequests(Checkers.equal(0))
          .storageExecutionTime(Checkers.greaterOrEqual(0.0))
          .plan(makePlanBuilder()
            .nodeType(NODE_INDEX_SCAN)
            .indexName("c1r1z2_index_x_plus_y")
            .build())
          .build());
      }
    }

    try (Connection connection1 = getConnectionBuilder().withTServer(2).connect();
        Statement statement1 = connection1.createStatement()) {

      assertOneRow(statement1, "SELECT yb_server_cloud()", "cloud1");
      assertOneRow(statement1, "SELECT yb_server_region()", "region2");
      assertOneRow(statement1, "SELECT yb_server_zone()", "zone1");

      if (enable_base_scans_cost_model)
        statement1.execute("SET yb_enable_base_scans_cost_model = true;");
      else
        statement1.execute("SET yb_enable_base_scans_cost_model = false;");

      ExplainAnalyzeUtils.testExplain(
        statement1,
        "SELECT x, y FROM foo WHERE x+y = 10",
        makeTopLevelBuilder()
          .storageReadRequests(Checkers.greater(0))
          .storageWriteRequests(Checkers.equal(0))
          .storageExecutionTime(Checkers.greaterOrEqual(0.0))
          .plan(makePlanBuilder()
            .nodeType(NODE_INDEX_ONLY_SCAN)
            .indexName("c1r2z1_index_x_plus_y")
            .build())
          .build());

      // Old cost model supports geolocation costing for index only scans only, but new cost model
      // should pick closest index for index scan too.
      if (enable_base_scans_cost_model) {
        ExplainAnalyzeUtils.testExplain(
        statement1,
        "SELECT * FROM foo WHERE x+y = 10",
        makeTopLevelBuilder()
          .storageReadRequests(Checkers.greater(0))
          .storageWriteRequests(Checkers.equal(0))
          .storageExecutionTime(Checkers.greaterOrEqual(0.0))
          .plan(makePlanBuilder()
            .nodeType(NODE_INDEX_SCAN)
            .indexName("c1r2z1_index_x_plus_y")
            .build())
          .build());
      }
    }

    // Try connecting to a different node, which should change the chosen index.
    try (Connection connection1 = getConnectionBuilder().withTServer(3).connect();
      Statement statement1 = connection1.createStatement()) {

      assertOneRow(statement1, "SELECT yb_server_cloud()", "cloud2");
      assertOneRow(statement1, "SELECT yb_server_region()", "region1");
      assertOneRow(statement1, "SELECT yb_server_zone()", "zone1");

      if (enable_base_scans_cost_model)
        statement1.execute("SET yb_enable_base_scans_cost_model = true;");
      else
        statement1.execute("SET yb_enable_base_scans_cost_model = false;");

      ExplainAnalyzeUtils.testExplain(
        statement1,
        "SELECT x, y FROM foo WHERE x+y = 10",
        makeTopLevelBuilder()
          .storageReadRequests(Checkers.greater(0))
          .storageWriteRequests(Checkers.equal(0))
          .storageExecutionTime(Checkers.greaterOrEqual(0.0))
          .plan(makePlanBuilder()
            .nodeType(NODE_INDEX_ONLY_SCAN)
            .indexName("c2r1z1_index_x_plus_y")
            .build())
          .build());

      // Old cost model supports geolocation costing for index only scans only, but new cost model
      // should pick closest index for index scan too.
      if (enable_base_scans_cost_model) {
        ExplainAnalyzeUtils.testExplain(
        statement1,
        "SELECT * FROM foo WHERE x+y = 10",
        makeTopLevelBuilder()
          .storageReadRequests(Checkers.greater(0))
          .storageWriteRequests(Checkers.equal(0))
          .storageExecutionTime(Checkers.greaterOrEqual(0.0))
          .plan(makePlanBuilder()
            .nodeType(NODE_INDEX_SCAN)
            .indexName("c2r1z1_index_x_plus_y")
            .build())
          .build());
      }
    }
  }

  @Test
  public void testYBTablespaceLocalExpressionIndex() throws Exception {
    // Test that leader preference is a cost component when calculating
    // the cost of identical indexes on different tablespaces. In particular,
    // this test checks that connecting to different nodes will result in
    // different indexes being chosen; it's otherwise very basic. More
    // tests for leader preference can be found in the postgres regression
    // tests for tablespaces.

    try (Connection connection1 = getConnectionBuilder().withTServer(0).connect();
        Statement statement1 = connection1.createStatement()) {
      // Create tablespaces, with the same placements but different leader preferences.
      statement1.execute("CREATE TABLESPACE c1r1z1 WITH (replica_placement=" +
          "'{\"num_replicas\":1, \"placement_blocks\":" +
          "[{\"cloud\":\"cloud1\",\"region\":\"region1\",\"zone\":\"zone1\"," +
          "\"min_num_replicas\":1}]}')");

      statement1.execute("CREATE TABLESPACE c1r1z2 WITH (replica_placement=" +
          "'{\"num_replicas\":1, \"placement_blocks\":" +
          "[{\"cloud\":\"cloud1\",\"region\":\"region1\",\"zone\":\"zone2\"," +
          "\"min_num_replicas\":1}]}')");

      statement1.execute("CREATE TABLESPACE c1r2z1 WITH (replica_placement=" +
          "'{\"num_replicas\":1, \"placement_blocks\":" +
          "[{\"cloud\":\"cloud1\",\"region\":\"region2\",\"zone\":\"zone1\"," +
          "\"min_num_replicas\":1}]}')");

      statement1.execute("CREATE TABLESPACE c2r1z1 WITH (replica_placement=" +
          "'{\"num_replicas\":1, \"placement_blocks\":" +
          "[{\"cloud\":\"cloud2\",\"region\":\"region1\",\"zone\":\"zone1\"," +
          "\"min_num_replicas\":1}]}')");

      statement1.executeUpdate("CREATE TABLE foo(x int, y int, z int)");
      statement1.executeUpdate("INSERT INTO foo (SELECT s, s, s FROM generate_series(1, 1000) s)");
      // Create identical indexes, one in each tablespace.
      statement1.executeUpdate("CREATE UNIQUE INDEX c1r1z1_index_x_plus_y ON foo((x+y)) "
          + "INCLUDE (x, y) TABLESPACE c1r1z1");
      statement1.executeUpdate("CREATE UNIQUE INDEX c1r1z2_index_x_plus_y ON foo((x+y)) "
          + "INCLUDE (x, y) TABLESPACE c1r1z2");
      statement1.executeUpdate("CREATE UNIQUE INDEX c1r2z1_index_x_plus_y ON foo((x+y)) "
          + "INCLUDE (x, y) TABLESPACE c1r2z1");
      statement1.executeUpdate("CREATE UNIQUE INDEX c2r1z1_index_x_plus_y ON foo((x+y)) "
          + "INCLUDE (x, y) TABLESPACE c2r1z1");
    }

    // Run without the cost model first. Table will be analyzed before running with cost model.
    testYBTablespaceLocalExpressionIndexHelper(false /* enable_base_scans_cost_model */);
    testYBTablespaceLocalExpressionIndexHelper(true /* enable_base_scans_cost_model */);
  }

  private void testYBTablespaceLeaderPreferenceHelper
      (Boolean enable_base_scans_cost_model) throws Exception {
    try (Connection connection1 = getConnectionBuilder().withTServer(0).connect();
        Statement statement1 = connection1.createStatement()) {

      assertOneRow(statement1, "SELECT yb_server_cloud()", "cloud1");
      assertOneRow(statement1, "SELECT yb_server_region()", "region1");
      assertOneRow(statement1, "SELECT yb_server_zone()", "zone1");

      if (enable_base_scans_cost_model)
      {
        statement1.execute("SET yb_enable_base_scans_cost_model = true;");
        statement1.execute("ANALYZE foo;");
      }
      else
        statement1.execute("SET yb_enable_base_scans_cost_model = false;");

      // Pick index on x with leader preference on c1r1z1 over c1r1z2
      ExplainAnalyzeUtils.testExplain(
        statement1,
        "SELECT * FROM foo WHERE x = 5",
        makeTopLevelBuilder()
          .storageReadRequests(Checkers.greater(0))
          .storageWriteRequests(Checkers.equal(0))
          .storageExecutionTime(Checkers.greaterOrEqual(0.0))
          .plan(makePlanBuilder()
            .nodeType(NODE_INDEX_ONLY_SCAN)
            .indexName("c1r1z1_index_x")
            .build())
          .build());

      // Pick index on y with leader preference on c1r1z1 over c1r2z1
      ExplainAnalyzeUtils.testExplain(
        statement1,
        "SELECT * FROM foo WHERE y = 5",
        makeTopLevelBuilder()
          .storageReadRequests(Checkers.greater(0))
          .storageWriteRequests(Checkers.equal(0))
          .storageExecutionTime(Checkers.greaterOrEqual(0.0))
          .plan(makePlanBuilder()
            .nodeType(NODE_INDEX_ONLY_SCAN)
            .indexName("c1r1z1_index_y")
            .build())
          .build());

      // Pick index on z with leader preference on c1r1z1 over c2r1z1
      ExplainAnalyzeUtils.testExplain(
        statement1,
        "SELECT * FROM foo WHERE z = 5",
        makeTopLevelBuilder()
          .storageReadRequests(Checkers.greater(0))
          .storageWriteRequests(Checkers.equal(0))
          .storageExecutionTime(Checkers.greaterOrEqual(0.0))
          .plan(makePlanBuilder()
            .nodeType(NODE_INDEX_ONLY_SCAN)
            .indexName("c1r1z1_index_z")
            .build())
          .build());
   }

   try (Connection connection1 = getConnectionBuilder().withTServer(1).connect();
        Statement statement1 = connection1.createStatement()) {

      assertOneRow(statement1, "SELECT yb_server_cloud()", "cloud1");
      assertOneRow(statement1, "SELECT yb_server_region()", "region1");
      assertOneRow(statement1, "SELECT yb_server_zone()", "zone2");

      if (enable_base_scans_cost_model)
        statement1.execute("SET yb_enable_base_scans_cost_model = true;");
      else
        statement1.execute("SET yb_enable_base_scans_cost_model = false;");

      // Pick index on x with leader preference on c1r1z2, over c1r1z1.
      ExplainAnalyzeUtils.testExplain(
        statement1,
        "SELECT * FROM foo WHERE x = 5",
        makeTopLevelBuilder()
          .storageReadRequests(Checkers.greater(0))
          .storageWriteRequests(Checkers.equal(0))
          .storageExecutionTime(Checkers.greaterOrEqual(0.0))
          .plan(makePlanBuilder()
            .nodeType(NODE_INDEX_ONLY_SCAN)
            .indexName("c1r1z2_index_x")
            .build())
          .build());
    }

    try (Connection connection1 = getConnectionBuilder().withTServer(2).connect();
        Statement statement1 = connection1.createStatement()) {

      assertOneRow(statement1, "SELECT yb_server_cloud()", "cloud1");
      assertOneRow(statement1, "SELECT yb_server_region()", "region2");
      assertOneRow(statement1, "SELECT yb_server_zone()", "zone1");

      if (enable_base_scans_cost_model)
        statement1.execute("SET yb_enable_base_scans_cost_model = true;");
      else
        statement1.execute("SET yb_enable_base_scans_cost_model = false;");

      // Pick index on y with leader preference on c1r2z1 over c1r1z1
      ExplainAnalyzeUtils.testExplain(
        statement1,
        "SELECT * FROM foo WHERE y = 5",
        makeTopLevelBuilder()
          .storageReadRequests(Checkers.greater(0))
          .storageWriteRequests(Checkers.equal(0))
          .storageExecutionTime(Checkers.greaterOrEqual(0.0))
          .plan(makePlanBuilder()
            .nodeType(NODE_INDEX_ONLY_SCAN)
            .indexName("c1r2z1_index_y")
            .build())
          .build());
    }

    // Try connecting to a different node, which should change the chosen index.
    try (Connection connection1 = getConnectionBuilder().withTServer(3).connect();
      Statement statement1 = connection1.createStatement()) {

      assertOneRow(statement1, "SELECT yb_server_cloud()", "cloud2");
      assertOneRow(statement1, "SELECT yb_server_region()", "region1");
      assertOneRow(statement1, "SELECT yb_server_zone()", "zone1");

      if (enable_base_scans_cost_model)
        statement1.execute("SET yb_enable_base_scans_cost_model = true;");
      else
        statement1.execute("SET yb_enable_base_scans_cost_model = false;");

      // Pick index on z with leader preference on c2r1z1 over c1r1z1
      ExplainAnalyzeUtils.testExplain(
        statement1,
        "SELECT * FROM foo WHERE z = 5",
        makeTopLevelBuilder()
          .storageReadRequests(Checkers.greater(0))
          .storageWriteRequests(Checkers.equal(0))
          .storageExecutionTime(Checkers.greaterOrEqual(0.0))
          .plan(makePlanBuilder()
            .nodeType(NODE_INDEX_ONLY_SCAN)
            .indexName("c2r1z1_index_z")
            .build())
          .build());
    }
  }

  @Test
  public void testYBTablespaceLeaderPreference() throws Exception {
    // Test that leader preference is a cost component when calculating
    // the cost of identical indexes on different tablespaces. In particular,
    // this test checks that connecting to different nodes will result in
    // different indexes being chosen; it's otherwise very basic. More
    // tests for leader preference can be found in the postgres regression
    // tests for tablespaces.

    try (Connection connection1 = getConnectionBuilder().withTServer(0).connect();
        Statement statement1 = connection1.createStatement()) {
      // Create tablespaces, with the same placements but different leader preferences.
      statement1.execute("CREATE TABLESPACE leader_c1r1z1_rep_c1r1z2 " +
          "  WITH (replica_placement=" +
          "'{\"num_replicas\":2, \"placement_blocks\":" +
          "[{\"cloud\":\"cloud1\",\"region\":\"region1\",\"zone\":\"zone1\"," +
          "\"min_num_replicas\":1, \"leader_preference\":1}," +
          "{\"cloud\":\"cloud1\",\"region\":\"region1\",\"zone\":\"zone2\"," +
          "\"min_num_replicas\":1}]}')");

      statement1.execute("CREATE TABLESPACE leader_c1r1z2_rep_c1r1z1 " +
          "  WITH (replica_placement=" +
          "'{\"num_replicas\":2, \"placement_blocks\":" +
          "[{\"cloud\":\"cloud1\",\"region\":\"region1\",\"zone\":\"zone2\"," +
          "\"min_num_replicas\":1, \"leader_preference\":1}," +
          "{\"cloud\":\"cloud1\",\"region\":\"region1\",\"zone\":\"zone1\"," +
          "\"min_num_replicas\":1}]}')");

      statement1.execute("CREATE TABLESPACE leader_c1r1z1_rep_c1r2z1 " +
          "  WITH (replica_placement=" +
          "'{\"num_replicas\":2, \"placement_blocks\":" +
          "[{\"cloud\":\"cloud1\",\"region\":\"region1\",\"zone\":\"zone1\"," +
          "\"min_num_replicas\":1, \"leader_preference\":1}," +
          "{\"cloud\":\"cloud1\",\"region\":\"region2\",\"zone\":\"zone1\"," +
          "\"min_num_replicas\":1}]}')");

      statement1.execute("CREATE TABLESPACE leader_c1r2z1_rep_c1r1z1 " +
          "  WITH (replica_placement=" +
          "'{\"num_replicas\":2, \"placement_blocks\":" +
          "[{\"cloud\":\"cloud1\",\"region\":\"region2\",\"zone\":\"zone1\"," +
          "\"min_num_replicas\":1, \"leader_preference\":1}," +
          "{\"cloud\":\"cloud1\",\"region\":\"region1\",\"zone\":\"zone1\"," +
          "\"min_num_replicas\":1}]}')");

      statement1.execute("CREATE TABLESPACE leader_c1r1z1_rep_c2r1z1 " +
          "  WITH (replica_placement=" +
          "'{\"num_replicas\":2, \"placement_blocks\":" +
          "[{\"cloud\":\"cloud1\",\"region\":\"region1\",\"zone\":\"zone1\"," +
          "\"min_num_replicas\":1, \"leader_preference\":1}," +
          "{\"cloud\":\"cloud2\",\"region\":\"region1\",\"zone\":\"zone1\"," +
          "\"min_num_replicas\":1}]}')");

      statement1.execute("CREATE TABLESPACE leader_c2r1z1_rep_c1r1z1 " +
          "  WITH (replica_placement=" +
          "'{\"num_replicas\":2, \"placement_blocks\":" +
          "[{\"cloud\":\"cloud2\",\"region\":\"region1\",\"zone\":\"zone1\"," +
          "\"min_num_replicas\":1, \"leader_preference\":1}," +
          "{\"cloud\":\"cloud1\",\"region\":\"region1\",\"zone\":\"zone1\"," +
          "\"min_num_replicas\":1}]}')");

      statement1.executeUpdate("CREATE TABLE foo(x int, y int, z int)");
      statement1.executeUpdate("INSERT INTO foo (SELECT s, s, s FROM generate_series(1, 1000) s)");
      // Create identical indexes, one in each tablespace.
      statement1.executeUpdate("CREATE UNIQUE INDEX c1r1z1_index_x ON foo(x) INCLUDE (y, z) "
          + "TABLESPACE leader_c1r1z1_rep_c1r1z2");
      statement1.executeUpdate("CREATE UNIQUE INDEX c1r1z2_index_x ON foo(x) INCLUDE (y, z) "
          + "TABLESPACE leader_c1r1z2_rep_c1r1z1");
      statement1.executeUpdate("CREATE UNIQUE INDEX c1r1z1_index_y ON foo(y) INCLUDE (x, z) "
          + "TABLESPACE leader_c1r1z1_rep_c1r2z1");
      statement1.executeUpdate("CREATE UNIQUE INDEX c1r2z1_index_y ON foo(y) INCLUDE (x, z) "
          + "TABLESPACE leader_c1r2z1_rep_c1r1z1");
      statement1.executeUpdate("CREATE UNIQUE INDEX c1r1z1_index_z ON foo(z) INCLUDE (x, y) "
          + "TABLESPACE leader_c1r1z1_rep_c2r1z1");
      statement1.executeUpdate("CREATE UNIQUE INDEX c2r1z1_index_z ON foo(z) INCLUDE (x, y) "
          + "TABLESPACE leader_c2r1z1_rep_c1r1z1");
    }

    // Run without the cost model first. Table will be analyzed before running with cost model.
    testYBTablespaceLeaderPreferenceHelper(false /* enable_base_scans_cost_model */);
    testYBTablespaceLeaderPreferenceHelper(true /* enable_base_scans_cost_model */);
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
          "[{\"cloud\":\"cloud2\",\"region\":\"region1\",\"zone\":\"zone1\"," +
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
    try (Connection connection2 = getConnectionBuilder().withTServer(3).connect();
         Statement statement2 = connection2.createStatement()) {
      assertEquals(getRowList(statement2, "SELECT * FROM localpartition").size(), 1);
    }
  }

  @Test
  public void testYbGeoDistributionHelperFunctions() throws Exception {
    // Verify that the functions return correct information.
    try (Connection connection1 = getConnectionBuilder().withTServer(0).connect();
         Statement statement1 = connection1.createStatement()) {
      assertOneRow(statement1, "SELECT yb_server_cloud()", "cloud1");
      assertOneRow(statement1, "SELECT yb_server_region()", "region1");
      assertOneRow(statement1, "SELECT yb_server_zone()", "zone1");
    }

    // Test column creation and row insertion with default values set to
    // yb_server_region(), yb_server_cloud(), yb_server_zone().
    try (Connection connection2 = getConnectionBuilder().withTServer(2).connect();
         Statement statement2 = connection2.createStatement()) {
      assertOneRow(statement2, "SELECT yb_server_cloud()", "cloud1");
      assertOneRow(statement2, "SELECT yb_server_region()", "region2");
      assertOneRow(statement2, "SELECT yb_server_zone()", "zone1");

      statement2.execute("CREATE TABLE tb (id INTEGER NOT NULL, " +
          "location VARCHAR DEFAULT yb_server_region(), " +
          "cloud VARCHAR DEFAULT yb_server_cloud(), " +
          "zone VARCHAR DEFAULT yb_server_zone(), " +
          "amount NUMERIC NOT NULL, " +
          "PRIMARY KEY (id, location)) " +
          "PARTITION BY LIST(location);");

      // Create partitioned tables by different values of location.
      statement2.executeUpdate("CREATE TABLE tb_loc1 PARTITION OF tb FOR VALUES IN ('loc1');");
      statement2.executeUpdate("CREATE TABLE tb_region1 PARTITION OF tb " +
                               "FOR VALUES IN ('region1');");
      statement2.executeUpdate("CREATE TABLE tb_region2 PARTITION OF tb " +
                               "FOR VALUES IN ('region2');");

      // Insert values into the partitioned tables. This node's region is region2.
      statement2.executeUpdate("INSERT INTO tb (id, location, amount) VALUES (1,'loc1', 10);");
      statement2.executeUpdate("INSERT INTO tb (id, location, cloud, zone, amount) " +
                               "VALUES (2,'region1', 'cloud1', 'zone2', 30);");
      statement2.executeUpdate("INSERT INTO tb (id, amount) VALUES (3, 20);");

      // Select the number of rows in tb in current region (region2).
      // Verify that we indeed get 1 row.
      assertEquals(getRowList(statement2,
        "SELECT * FROM tb WHERE location=yb_server_region();").size(), 1);
    }

    // Connect to a different node and verify that the functions' return values get
    // updated correctly
    try (Connection connection3 = getConnectionBuilder().withTServer(3).connect();
         Statement statement3 = connection3.createStatement()) {
      assertOneRow(statement3, "SELECT yb_server_cloud()", "cloud2");
      assertOneRow(statement3, "SELECT yb_server_region()", "region1");
      assertOneRow(statement3, "SELECT yb_server_zone()", "zone1");

      // Insert values into partitioned table. This node's region is region1.
      statement3.executeUpdate("INSERT INTO tb (id, amount) VALUES (4, 30);");

      // Select the number of rows in tb_region1, verify that we indeed get 2 rows.
      assertEquals(getRowList(statement3, "SELECT * FROM tb_region1;").size(), 2);

      // Verify Explain Select query for partition pruning.
      String query = "EXPLAIN (COSTS OFF) SELECT * FROM tb WHERE location=yb_server_region();";
      List<Row> rows = getRowList(statement3, query);
      assertTrue(rows.toString().contains("Seq Scan on tb_region1"));

      query = "EXPLAIN (COSTS OFF) SELECT * FROM tb WHERE id=2 and location=yb_server_region() " +
              "UNION ALL SELECT * FROM tb WHERE id=2 LIMIT 1;";
      rows = getRowList(statement3, query);
      assertTrue(rows.toString().contains("Index Cond: ((id = 2) AND " +
        "((location)::text = (yb_server_region())::text))"));
    }
  }
}
