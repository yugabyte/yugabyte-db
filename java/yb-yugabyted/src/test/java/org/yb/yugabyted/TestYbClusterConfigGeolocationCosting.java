package org.yb.yugabyted;

import org.yb.pgsql.ExplainAnalyzeUtils;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_INDEX_ONLY_SCAN;
import static org.yb.pgsql.ExplainAnalyzeUtils.NODE_INDEX_SCAN;
import static org.yb.pgsql.ExplainAnalyzeUtils.makePlanBuilder;
import static org.yb.pgsql.ExplainAnalyzeUtils.makeTopLevelBuilder;

import java.sql.Connection;
import java.sql.Statement;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Map;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.YBTestRunner;
import org.yb.minicluster.MiniYugabytedCluster;
import org.yb.minicluster.MiniYugabytedClusterParameters;
import org.yb.minicluster.MiniYugabytedNodeConfigurations;
import org.yb.util.json.Checkers;
import org.yb.yugabyted.BaseYbdClientTest;

@RunWith(value = YBTestRunner.class)
  public class TestYbClusterConfigGeolocationCosting extends BaseYbdClientTest {
  private static final Logger LOG =
    LoggerFactory.getLogger(TestYbClusterConfigGeolocationCosting.class);

  public TestYbClusterConfigGeolocationCosting() {
    clusterParameters = new MiniYugabytedClusterParameters.Builder()
      .numNodes(1)
      .build();

    clusterConfigurations = new ArrayList<>();
    MiniYugabytedNodeConfigurations nodeConfigurations = new
      MiniYugabytedNodeConfigurations.Builder()
      .build();

    clusterConfigurations.add(nodeConfigurations);
  }

  static final double LOCAL_ZONE_STARTUP_COST = 21.08;
  static final double INTER_ZONE_STARTUP_COST = 23.08;
  static final double INTER_REGION_STARTUP_COST = 41.08;
  static final double INTER_CLOUD_STARTUP_COST = 41.08;
  static final double STARTUP_COST_EPSILON = 0.2;

  static void checkIndexScanPlan(Statement stmt, String query, String nodeType,
                                 String indexName) throws Exception {
    ExplainAnalyzeUtils.testExplainDebug(stmt, query,
      makeTopLevelBuilder()
        .plan(makePlanBuilder()
          .nodeType(nodeType)
          .indexName(indexName)
          .build())
        .build());
  }

  static void checkStartupCost(Statement stmt, String query,
                               double expectedStartupCost) throws Exception {
    ExplainAnalyzeUtils.testExplainDebug(stmt, query,
      makeTopLevelBuilder()
        .plan(makePlanBuilder()
          .startupCost(Checkers.equal(expectedStartupCost, STARTUP_COST_EPSILON))
          .build())
        .build());
  }

  private void createCluster(int numClouds,
                             int numRegionsPerCloud,
                             int numZonesPerRegion,
                             boolean withLeaderPreference) throws Exception {
    String dataPlacement = "";
    clusterParameters = new MiniYugabytedClusterParameters.Builder()
      .numNodes(numClouds * numRegionsPerCloud * numZonesPerRegion)
      .build();

    clusterConfigurations = new ArrayList<>();
    int leaderPreference = 1;
    for (int i = 0; i < numClouds; i++) {
      for (int j = 0; j < numRegionsPerCloud; j++) {
        for (int k = 0; k < numZonesPerRegion; k++) {
          Map<String, String> flags = new HashMap<>();
          String cloudLocation = "c" + i + ".r" + j + ".z" + k;
          flags.put("cloud_location", cloudLocation);

          clusterConfigurations.add(new MiniYugabytedNodeConfigurations.Builder()
            .yugabytedFlags(flags)
            .build());

          if (leaderPreference > 1) {
            dataPlacement += ",";
          }

          dataPlacement += cloudLocation + ":" + (leaderPreference++);
        }
      }
    }

    destroyMiniCluster();
    createMiniYugabytedCluster(clusterParameters.numNodes);

    Thread.sleep(30000);

    if (withLeaderPreference) {
      configureDataPlacement(dataPlacement);
      Thread.sleep(10000);
    }
  }

  private Connection connectToTserverNode(int tserverIndex) throws Exception {
    return getConnectionBuilder()
      .withDatabase("yugabyte")
      .withUser("yugabyte")
      .withPassword("yugabyte")
      .withTServer(tserverIndex)
      .connect();
  }

  @Test
  public void testYbSingleNodeCluster() throws Exception {
    createCluster(1, 1, 1, false);

    String clusterUuid = syncClient.getMasterClusterConfig().getConfig().getClusterUuid();
    LOG.info("Cluster UUID: " + clusterUuid);

    try (Connection conn = connectToTserverNode(0)) {
      try (Statement stmt = conn.createStatement()) {
        stmt.execute("CREATE TABLE test (k1 INT, v1 INT)");
        stmt.execute("INSERT INTO test (SELECT s, s FROM generate_series(1, 12345) s)");
        stmt.execute("ANALYZE test");
        checkStartupCost(stmt, "SELECT * FROM test", LOCAL_ZONE_STARTUP_COST);
      }
    }

    destroyMiniCluster();
  }

  @Test
  public void testYbMultiZoneCluster() throws Exception {
    createCluster(1, 1, 3, false);

    String clusterUuid = syncClient.getMasterClusterConfig().getConfig().getClusterUuid();
    LOG.info("Cluster UUID: " + clusterUuid);

    try (Connection conn = connectToTserverNode(0)) {
      try (Statement stmt = conn.createStatement()) {
        stmt.execute("CREATE TABLE test (k1 INT, v1 INT, PRIMARY KEY (k1 ASC))");
        stmt.execute("INSERT INTO test (SELECT s, s FROM generate_series(1, 12345) s)");
        stmt.execute("ANALYZE test");
        checkStartupCost(stmt, "SELECT * FROM test", INTER_ZONE_STARTUP_COST);

        // Tablespace with only 1 replica in c0.r0.z0
        stmt.execute("CREATE TABLESPACE ts_c0_r0_z0 WITH (replica_placement='" +
            "{\"num_replicas\": 1, \"placement_blocks\": [{\"cloud\": \"c0\", " +
            "\"region\": \"r0\", \"zone\": \"z0\", \"min_num_replicas\": 1}]}');");
        stmt.execute("CREATE INDEX test_idx_c0_r0_z0 ON test (k1 ASC) TABLESPACE ts_c0_r0_z0");
        // Local index should be preferred over primary index that is distributed across zones.
        checkIndexScanPlan(stmt, "SELECT k1 FROM test WHERE k1 < 1234",
            NODE_INDEX_ONLY_SCAN, "test_idx_c0_r0_z0");
        checkIndexScanPlan(stmt,
            "/*+ IndexScan(test test_pkey) */ SELECT k1 FROM test WHERE k1 < 1234",
            NODE_INDEX_SCAN, "test_pkey");

        stmt.execute("CREATE TABLE t_c0_r0_z0 (k1 INT, v1 INT) TABLESPACE ts_c0_r0_z0");
        stmt.execute("INSERT INTO t_c0_r0_z0 (SELECT s, s FROM generate_series(1, 12345) s)");
        stmt.execute("ANALYZE t_c0_r0_z0");
        checkStartupCost(stmt, "SELECT * FROM t_c0_r0_z0", LOCAL_ZONE_STARTUP_COST);

        stmt.execute("CREATE TABLESPACE ts_multi_zone WITH (replica_placement='" +
            "{\"num_replicas\": 3, \"placement_blocks\": " +
            "[{\"cloud\":\"c0\", \"region\":\"r0\", \"zone\":\"z0\", \"min_num_replicas\":1}, " +
            "{\"cloud\":\"c0\", \"region\":\"r0\", \"zone\":\"z1\", \"min_num_replicas\":1}, " +
            "{\"cloud\":\"c0\", \"region\":\"r0\", \"zone\":\"z2\", \"min_num_replicas\":1}]}');");
        stmt.execute("CREATE TABLE t_multi_zone (k1 INT, v1 INT) TABLESPACE ts_multi_zone");
        stmt.execute("INSERT INTO t_multi_zone (SELECT s, s FROM generate_series(1, 12345) s)");
        stmt.execute("ANALYZE t_multi_zone");
        checkStartupCost(stmt, "SELECT * FROM t_multi_zone", INTER_ZONE_STARTUP_COST);
      }
    }

    try (Connection conn = connectToTserverNode(1)) {
      try (Statement stmt = conn.createStatement()) {
        checkStartupCost(stmt, "SELECT * FROM test", INTER_ZONE_STARTUP_COST);
        checkStartupCost(stmt, "SELECT v1 FROM test WHERE v1 = 1234", INTER_ZONE_STARTUP_COST);
        checkStartupCost(stmt, "SELECT * FROM t_c0_r0_z0", INTER_ZONE_STARTUP_COST);
        checkStartupCost(stmt, "SELECT * FROM t_multi_zone", INTER_ZONE_STARTUP_COST);
      }
    }

    destroyMiniCluster();
  }

  @Test
  public void testYbMultiRegionCluster() throws Exception {
    createCluster(1, 3, 1, false);

    String clusterUuid = syncClient.getMasterClusterConfig().getConfig().getClusterUuid();
    LOG.info("Cluster UUID: " + clusterUuid);

    try (Connection conn = connectToTserverNode(0)) {
      try (Statement stmt = conn.createStatement()) {
        stmt.execute("CREATE TABLE test (k1 INT, v1 INT, PRIMARY KEY (k1 ASC))");
        stmt.execute("INSERT INTO test (SELECT s, s FROM generate_series(1, 12345) s)");
        stmt.execute("ANALYZE test");
        checkStartupCost(stmt, "SELECT * FROM test", INTER_REGION_STARTUP_COST);

        // Tablespace with only 1 replica in c0.r0.z0
        stmt.execute("CREATE TABLESPACE ts_c0_r0_z0 WITH (replica_placement='" +
            "{\"num_replicas\": 1, \"placement_blocks\": " +
            "[{\"cloud\":\"c0\", \"region\":\"r0\", \"zone\":\"z0\", \"min_num_replicas\":1}]}');");
        stmt.execute("CREATE INDEX test_idx_c0_r0_z0 ON test (k1 ASC) TABLESPACE ts_c0_r0_z0");
        // Local index should be preferred over primary index that is distributed across zones.
        checkIndexScanPlan(stmt, "SELECT k1 FROM test WHERE k1 < 1234",
            NODE_INDEX_ONLY_SCAN, "test_idx_c0_r0_z0");
        checkIndexScanPlan(stmt,
            "/*+ IndexScan(test test_pkey) */ SELECT k1 FROM test WHERE k1 < 1234",
            NODE_INDEX_SCAN, "test_pkey");

        // Costing for tables with tablespaces shoudl work as expected.
        stmt.execute("CREATE TABLE t_c0_r0_z0 (k1 INT, v1 INT) TABLESPACE ts_c0_r0_z0");
        stmt.execute("INSERT INTO t_c0_r0_z0 (SELECT s, s FROM generate_series(1, 12345) s)");
        stmt.execute("ANALYZE t_c0_r0_z0");
        checkStartupCost(stmt, "SELECT * FROM t_c0_r0_z0", LOCAL_ZONE_STARTUP_COST);

        stmt.execute("CREATE TABLESPACE ts_c0_r012_z0 WITH (replica_placement='" +
            "{\"num_replicas\": 3, \"placement_blocks\": " +
            "[{\"cloud\":\"c0\", \"region\":\"r0\", \"zone\":\"z0\", \"min_num_replicas\":1}, " +
            "{\"cloud\":\"c0\", \"region\":\"r1\", \"zone\":\"z0\", \"min_num_replicas\":1}, " +
            "{\"cloud\":\"c0\", \"region\":\"r2\", \"zone\":\"z0\", \"min_num_replicas\":1}]}');");
        stmt.execute("CREATE TABLE t_c0_r012_z0 (k1 INT, v1 INT) TABLESPACE ts_c0_r012_z0");
        stmt.execute("INSERT INTO t_c0_r012_z0 (SELECT s, s FROM generate_series(1, 12345) s)");
        stmt.execute("ANALYZE t_c0_r012_z0");
        checkStartupCost(stmt, "SELECT * FROM t_c0_r012_z0", INTER_REGION_STARTUP_COST);
      }
    }

    try (Connection conn = connectToTserverNode(2)) {
      try (Statement stmt = conn.createStatement()) {
        checkStartupCost(stmt, "SELECT * FROM test", INTER_REGION_STARTUP_COST);
        checkStartupCost(stmt, "SELECT * FROM t_c0_r0_z0", INTER_REGION_STARTUP_COST);
        checkStartupCost(stmt, "SELECT * FROM t_c0_r012_z0", INTER_REGION_STARTUP_COST);
      }
    }

    destroyMiniCluster();
  }

  @Test
  public void testYbMultiCloudCluster() throws Exception {
    createCluster(3, 1, 1, false);

    String clusterUuid = syncClient.getMasterClusterConfig().getConfig().getClusterUuid();
    LOG.info("Cluster UUID: " + clusterUuid);

    try (Connection conn = connectToTserverNode(0)) {
      try (Statement stmt = conn.createStatement()) {
        stmt.execute("CREATE TABLE test (k1 INT, v1 INT, PRIMARY KEY (k1 ASC))");
        stmt.execute("INSERT INTO test (SELECT s, s FROM generate_series(1, 12345) s)");
        stmt.execute("ANALYZE test");
        checkStartupCost(stmt, "SELECT * FROM test", INTER_REGION_STARTUP_COST);

        // Tablespace with only 1 replica in c0.r0.z0
        stmt.execute("CREATE TABLESPACE ts_c0_r0_z0 WITH (replica_placement='" +
            "{\"num_replicas\": 1, \"placement_blocks\": " +
            "[{\"cloud\":\"c0\", \"region\":\"r0\", \"zone\":\"z0\", \"min_num_replicas\":1}]}');");
        stmt.execute("CREATE INDEX test_idx_c0_r0_z0 ON test (k1 ASC) TABLESPACE ts_c0_r0_z0");
        // Local index should be preferred over primary index that is distributed across zones.
        checkIndexScanPlan(stmt, "SELECT k1 FROM test WHERE k1 < 1234",
            NODE_INDEX_ONLY_SCAN, "test_idx_c0_r0_z0");
        checkIndexScanPlan(stmt,
            "/*+ IndexScan(test test_pkey) */ SELECT k1 FROM test WHERE k1 < 1234",
            NODE_INDEX_SCAN, "test_pkey");

        // Costing for tables with tablespaces shoudl work as expected.
        stmt.execute("CREATE TABLE t_c0_r0_z0 (k1 INT, v1 INT) TABLESPACE ts_c0_r0_z0");
        stmt.execute("INSERT INTO t_c0_r0_z0 (SELECT s, s FROM generate_series(1, 12345) s)");
        stmt.execute("ANALYZE t_c0_r0_z0");
        checkStartupCost(stmt, "SELECT * FROM t_c0_r0_z0", LOCAL_ZONE_STARTUP_COST);

        stmt.execute("CREATE TABLESPACE ts_c012_r0_z0 WITH (replica_placement='" +
            "{\"num_replicas\": 3, \"placement_blocks\": " +
            "[{\"cloud\":\"c0\", \"region\":\"r0\", \"zone\":\"z0\", \"min_num_replicas\":1}, " +
            "{\"cloud\":\"c1\", \"region\":\"r0\", \"zone\":\"z0\", \"min_num_replicas\":1}, " +
            "{\"cloud\":\"c2\", \"region\":\"r0\", \"zone\":\"z0\", \"min_num_replicas\":1}]}');");
        stmt.execute("CREATE TABLE t_c012_r0_z0 (k1 INT, v1 INT) TABLESPACE ts_c012_r0_z0");
        stmt.execute("INSERT INTO t_c012_r0_z0 (SELECT s, s FROM generate_series(1, 12345) s)");
        stmt.execute("ANALYZE t_c012_r0_z0");
        checkStartupCost(stmt, "SELECT * FROM t_c012_r0_z0", INTER_CLOUD_STARTUP_COST);
      }
    }

    try (Connection conn = connectToTserverNode(1)) {
      try (Statement stmt = conn.createStatement()) {
        checkStartupCost(stmt, "SELECT * FROM test", INTER_CLOUD_STARTUP_COST);
        checkStartupCost(stmt, "SELECT * FROM t_c0_r0_z0", INTER_CLOUD_STARTUP_COST);
        checkStartupCost(stmt, "SELECT * FROM t_c012_r0_z0", INTER_CLOUD_STARTUP_COST);
      }
    }

    destroyMiniCluster();
  }

  @Test
  public void testYbMultiZoneClusterWithLeaderPreference() throws Exception {
    createCluster(1, 1, 3, true);

    String clusterUuid = syncClient.getMasterClusterConfig().getConfig().getClusterUuid();
    LOG.info("Cluster UUID: " + clusterUuid);

    try (Connection conn = connectToTserverNode(0)) {
      try (Statement stmt = conn.createStatement()) {
        stmt.execute("CREATE TABLE test (k1 INT, v1 INT, PRIMARY KEY (k1 ASC))");
        stmt.execute("INSERT INTO test (SELECT s, s FROM generate_series(1, 12345) s)");
        stmt.execute("ANALYZE test");
        checkStartupCost(stmt, "SELECT * FROM test", LOCAL_ZONE_STARTUP_COST);
      }
    }

    try (Connection conn = connectToTserverNode(1)) {
      try (Statement stmt = conn.createStatement()) {
        checkStartupCost(stmt, "SELECT * FROM test", INTER_ZONE_STARTUP_COST);
      }
    }

    destroyMiniCluster();
  }
}
