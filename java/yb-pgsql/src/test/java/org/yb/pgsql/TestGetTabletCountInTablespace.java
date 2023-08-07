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

import java.sql.Connection;
import java.sql.Statement;
import java.util.Arrays;
import java.util.function.Consumer;
import java.util.List;
import java.util.Map;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.client.YBTable;
import org.yb.YBTestRunner;
import org.yb.minicluster.MiniYBClusterBuilder;
import org.yb.util.BuildTypeUtil;

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertNotNull;

@RunWith(value = YBTestRunner.class)
public class TestGetTabletCountInTablespace extends BasePgSQLTest {

  private List<Map<String, String>> defaultClusterPlacementInfo = Arrays.asList(
      ImmutableMap.of(
          "placement_cloud", "c1",
          "placement_region", "r1",
          "placement_zone", "z1"),
      ImmutableMap.of(
          "placement_cloud", "c1",
          "placement_region", "r1",
          "placement_zone", "z2"),
      ImmutableMap.of(
          "placement_cloud", "c1",
          "placement_region", "r1",
          "placement_zone", "z3"));

 @Override
  protected void customizeMiniClusterBuilder(MiniYBClusterBuilder builder) {
    super.customizeMiniClusterBuilder(builder);
    builder.enablePgTransactions(true);
    builder.perTServerFlags(defaultClusterPlacementInfo);
    builder.numTservers(defaultClusterPlacementInfo.size());
  }

  void createTableWithinTablespace(Statement stmt, String tablespace,
                                   String table, String tableSpacePlacementInfo)
      throws Exception {
    stmt.execute(String.format("CREATE TABLESPACE %s WITH (replica_placement='%s')",
        tablespace, tableSpacePlacementInfo));
    stmt.execute(String.format("CREATE TABLE %s (id INTEGER, field TEXT) TABLESPACE %s",
        table, tablespace));
  }

  int getTabletsLocationsCount(String tableName) throws Exception {
    // Retrieves all tables with same substring
    List<YBTable> ybTables = getTablesFromName(tableName);
    YBTable ybTable = null;
    for (YBTable thisTable : ybTables) {
      if (tableName.equals(thisTable.getName())) {
        ybTable = thisTable;
      }
    }
    assertNotNull(ybTable);
    return ybTable.getTabletsLocations(30000).size();
  }

  @Test
  public void testMultiZone1() throws Exception {
    try (Connection conn = getConnectionBuilder().withTServer(0).connect();
         Statement stmt = conn.createStatement()) {
      String tablespace = "multi_zone_tablespace_all_zones";
      String table ="multi_zone_tablespace_table_all_zones";
      String tableSpacePlacementInfo =
          "{\"num_replicas\": 3, \"placement_blocks\":" +
              "[{\"cloud\":\"c1\",\"region\":\"r1\"," +
                "\"zone\":\"z1\",\"min_num_replicas\":1}," +
               "{\"cloud\":\"c1\",\"region\":\"r1\"," +
                "\"zone\":\"z2\",\"min_num_replicas\":1}," +
               "{\"cloud\":\"c1\",\"region\":\"r1\"," +
                "\"zone\":\"z3\",\"min_num_replicas\":1}]}";

      createTableWithinTablespace(stmt, tablespace, table, tableSpacePlacementInfo);
      assertEquals(3, getTabletsLocationsCount(table));
    }
  }

  @Test
  public void testMultiZone2() throws Exception {
    try (Connection conn = getConnectionBuilder().withTServer(0).connect();
         Statement stmt = conn.createStatement()) {
      String tablespace = "multi_zone_tablespace_part_zones";
      String table ="multi_zone_tablespace_table_part_zones";
      String tableSpacePlacementInfo =
          "{\"num_replicas\": 2, \"placement_blocks\":" +
              "[{\"cloud\":\"c1\",\"region\":\"r1\"," +
                "\"zone\":\"z1\",\"min_num_replicas\":1}," +
               "{\"cloud\":\"c1\",\"region\":\"r1\"," +
                "\"zone\":\"z2\",\"min_num_replicas\":1}]}";

      createTableWithinTablespace(stmt, tablespace, table, tableSpacePlacementInfo);
      assertEquals(2, getTabletsLocationsCount(table));
    }
  }

  @Test
  public void testMultiZoneWithNumTabletsFlags1() throws Exception {
    List<Map<String, String>> placementInfo = Arrays.asList(
        ImmutableMap.of(
            "placement_cloud", "c1",
            "placement_region", "r1",
            "placement_zone", "z1"),
        ImmutableMap.of(
            "placement_cloud", "c1",
            "placement_region", "r1",
            "placement_zone", "z2"),
        ImmutableMap.of(
            "placement_cloud", "c1",
            "placement_region", "r1",
            "placement_zone", "z3"));
    Consumer<MiniYBClusterBuilder> builder = x -> {
      x.perTServerFlags(placementInfo);
      x.numShardsPerTServer(-1);
      x.addCommonTServerFlag("num_cpus", "4");
      x.addCommonTServerFlag("ysql_num_tablets", "-1");
      x.addCommonTServerFlag("ycql_num_tablets", "5");
    };
    restartClusterWithClusterBuilder(builder);

    try (Connection conn = getConnectionBuilder().withTServer(0).connect();
         Statement stmt = conn.createStatement()) {
       String tablespace = "multi_zone_tablespace_with_ycql_num_tablets";
       String table ="multi_zone_tablespace_table_with_ycql_num_tablets";
       String tableSpacePlacementInfo =
           "{\"num_replicas\": 3, \"placement_blocks\":" +
               "[{\"cloud\":\"c1\",\"region\":\"r1\"," +
                 "\"zone\":\"z1\",\"min_num_replicas\":1}," +
                "{\"cloud\":\"c1\",\"region\":\"r1\"," +
                 "\"zone\":\"z2\",\"min_num_replicas\":1}," +
                "{\"cloud\":\"c1\",\"region\":\"r1\"," +
                 "\"zone\":\"z3\",\"min_num_replicas\":1}]}";

      createTableWithinTablespace(stmt, tablespace, table, tableSpacePlacementInfo);
      // Adheres to placement info since ysql_num_tablets flag isn't set.
      // Should not adhere to ycql_num_tablets flag even though it's set.
      // Since automatic_tablet_splitting_enabled is set,
      // we take min(2, num_tablets) = min(2, 3) = 2.
      assertEquals(2, getTabletsLocationsCount(table));
    }
  }

  @Test
  public void testMultiZoneWithNumTabletsFlags2() throws Exception {
    List<Map<String, String>> placementInfo = Arrays.asList(
        ImmutableMap.of(
            "placement_cloud", "c1",
            "placement_region", "r1",
            "placement_zone", "z1"),
        ImmutableMap.of(
            "placement_cloud", "c1",
            "placement_region", "r1",
            "placement_zone", "z2"),
        ImmutableMap.of(
            "placement_cloud", "c1",
            "placement_region", "r1",
            "placement_zone", "z3"));
    Consumer<MiniYBClusterBuilder> builder = x -> {
      x.perTServerFlags(placementInfo);
      x.numShardsPerTServer(-1);
      x.addCommonTServerFlag("ysql_num_tablets", "5");
      x.addCommonTServerFlag("ycql_num_tablets", "1");
    };
    restartClusterWithClusterBuilder(builder);

    try (Connection conn = getConnectionBuilder().withTServer(0).connect();
         Statement stmt = conn.createStatement()) {
       String tablespace = "multi_zone_tablespace_with_ycql_num_tablets";
       String table ="multi_zone_tablespace_table_with_ycql_num_tablets";
       String tableSpacePlacementInfo =
           "{\"num_replicas\": 2, \"placement_blocks\":" +
               "[{\"cloud\":\"c1\",\"region\":\"r1\"," +
                 "\"zone\":\"z1\",\"min_num_replicas\":1}," +
                "{\"cloud\":\"c1\",\"region\":\"r1\"," +
                 "\"zone\":\"z2\",\"min_num_replicas\":1}]}";

      createTableWithinTablespace(stmt, tablespace, table, tableSpacePlacementInfo);
      // Adheres to ysql_num_tablets flag value as it's set.
      assertEquals(5, getTabletsLocationsCount(table));
    }
  }

  @Test
  public void testSingleZone() throws Exception {
    try (Connection conn = getConnectionBuilder().withTServer(0).connect();
         Statement stmt = conn.createStatement()) {
      String tablespace = "multi_zone_tablespace_one_zone";
      String table ="multi_zone_tablespace_table_one_zone";
      String tableSpacePlacementInfo =
          "{\"num_replicas\": 1, \"placement_blocks\":" +
              "[{\"cloud\":\"c1\",\"region\":\"r1\"," +
                "\"zone\":\"z1\",\"min_num_replicas\":1}]}";

      createTableWithinTablespace(stmt, tablespace, table, tableSpacePlacementInfo);
      assertEquals(1, getTabletsLocationsCount(table));
    }
  }

  @Test
  public void testMultiZoneWithSplit() throws Exception {
    try (Connection conn = getConnectionBuilder().withTServer(0).connect();
         Statement stmt = conn.createStatement()) {
      String tablespace = "multi_zone_tablespace_part_zones_with_split";
      String table ="multi_zone_tablespace_table_part_zones";
      String tableSpacePlacementInfo =
          "{\"num_replicas\": 2, \"placement_blocks\":" +
              "[{\"cloud\":\"c1\",\"region\":\"r1\"," +
                "\"zone\":\"z1\",\"min_num_replicas\":1}," +
                "{\"cloud\":\"c1\",\"region\":\"r1\"," +
                "\"zone\":\"z2\",\"min_num_replicas\":1}]}";

      stmt.execute(String.format("CREATE TABLESPACE %s WITH (replica_placement='%s')",
          tablespace, tableSpacePlacementInfo));

      // SPLIT INTO
      String table1 = table + "_split_into_1";
      stmt.execute(String.format("CREATE TABLE %s (id INT PRIMARY KEY)" +
                                 "TABLESPACE %s SPLIT INTO 1 TABLETS",
                   table1, tablespace));
      assertEquals(1, getTabletsLocationsCount(table1));

      String table2 = table + "_split_into_3";
      stmt.execute(String.format("CREATE TABLE %s (id INT PRIMARY KEY)" +
                                 "TABLESPACE %s SPLIT INTO 3 TABLETS",
                   table2, tablespace));
      assertEquals(3, getTabletsLocationsCount(table2));

      // SPLIT AT VALUE
      String table3 = table + "_split_at_values_2_parts";
      stmt.execute(String.format("CREATE TABLE %s (id INT NOT NULL, PRIMARY KEY (id ASC)) " +
                                 "TABLESPACE %s SPLIT AT VALUES ((2))",
                   table3, tablespace));
      assertEquals(2, getTabletsLocationsCount(table3));
      String table4 = table + "_split_at_values_4_parts";
      stmt.execute(String.format("CREATE TABLE %s (id INT NOT NULL, PRIMARY KEY (id ASC)) " +
                                 "TABLESPACE %s SPLIT AT VALUES ((2), (5), (8))",
                   table4, tablespace));
      assertEquals(4, getTabletsLocationsCount(table4));
    }
  }

  @Test
  public void testMultiRegion() throws Exception {
    List<Map<String, String>> placementInfo = Arrays.asList(
        ImmutableMap.of(
            "placement_cloud", "c1",
            "placement_region", "r1",
            "placement_zone", "z1"),
        ImmutableMap.of(
            "placement_cloud", "c1",
            "placement_region", "r2",
            "placement_zone", "z1"),
        ImmutableMap.of(
            "placement_cloud", "c1",
            "placement_region", "r3",
            "placement_zone", "z1"));
    Consumer<MiniYBClusterBuilder> builder = x -> x.perTServerFlags(placementInfo);
    restartClusterWithClusterBuilder(builder);

    try (Connection conn = getConnectionBuilder().withTServer(0).connect();
         Statement stmt = conn.createStatement()) {
       String tablespace = "multi_region_tablespace_part_zones";
       String table ="multi_region_tablespace_table_part_zones";
       String tableSpacePlacementInfo =
           "{\"num_replicas\": 2, \"placement_blocks\":" +
               "[{\"cloud\":\"c1\",\"region\":\"r1\"," +
                 "\"zone\":\"z1\",\"min_num_replicas\":1}," +
                "{\"cloud\":\"c1\",\"region\":\"r2\"," +
                 "\"zone\":\"z1\",\"min_num_replicas\":1}]}";

      createTableWithinTablespace(stmt, tablespace, table, tableSpacePlacementInfo);
      assertEquals(2, getTabletsLocationsCount(table));
    }
  }

  @Test
  public void testMultiZoneWithMultiShardsPerTServer() throws Exception {
    List<Map<String, String>> placementInfo = Arrays.asList(
        ImmutableMap.of(
            "placement_cloud", "c1",
            "placement_region", "r1",
            "placement_zone", "z1"),
        ImmutableMap.of(
            "placement_cloud", "c1",
            "placement_region", "r1",
            "placement_zone", "z2"),
        ImmutableMap.of(
            "placement_cloud", "c1",
            "placement_region", "r1",
            "placement_zone", "z3"));
    Consumer<MiniYBClusterBuilder> builder = x -> {
      x.perTServerFlags(placementInfo);
      x.numShardsPerTServer(3);
    };
    restartClusterWithClusterBuilder(builder);

    try (Connection conn = getConnectionBuilder().withTServer(0).connect();
         Statement stmt = conn.createStatement()) {
       String tablespace = "multi_zone_tablespace_with_multi_shards";
       String table ="multi_zone_tablespace_table_with_multi_shards";
       String tableSpacePlacementInfo =
           "{\"num_replicas\": 2, \"placement_blocks\":" +
               "[{\"cloud\":\"c1\",\"region\":\"r1\"," +
                 "\"zone\":\"z1\",\"min_num_replicas\":1}," +
                "{\"cloud\":\"c1\",\"region\":\"r1\"," +
                 "\"zone\":\"z2\",\"min_num_replicas\":1}]}";

      createTableWithinTablespace(stmt, tablespace, table, tableSpacePlacementInfo);
      assertEquals(6, getTabletsLocationsCount(table));
    }
  }

  @Test
  public void testMultiZoneWithCpu1() throws Exception {
    List<Map<String, String>> placementInfo = Arrays.asList(
        ImmutableMap.of(
            "placement_cloud", "c1",
            "placement_region", "r1",
            "placement_zone", "z1"),
        ImmutableMap.of(
            "placement_cloud", "c1",
            "placement_region", "r1",
            "placement_zone", "z2"),
        ImmutableMap.of(
            "placement_cloud", "c1",
            "placement_region", "r1",
            "placement_zone", "z3"));
    Consumer<MiniYBClusterBuilder> builder = x -> {
      x.perTServerFlags(placementInfo);
      x.numShardsPerTServer(-1);
      x.addCommonTServerFlag("num_cpus", "2");
      x.addMasterFlag("num_cpus", "4");
    };
    restartClusterWithClusterBuilder(builder);

    try (Connection conn = getConnectionBuilder().withTServer(0).connect();
         Statement stmt = conn.createStatement()) {
       String tablespace = "multi_zone_tablespace_with_multi_shards";
       String table ="multi_zone_tablespace_table_with_multi_shards";
       String tableSpacePlacementInfo =
           "{\"num_replicas\": 2, \"placement_blocks\":" +
               "[{\"cloud\":\"c1\",\"region\":\"r1\"," +
                 "\"zone\":\"z1\",\"min_num_replicas\":1}," +
                "{\"cloud\":\"c1\",\"region\":\"r1\"," +
                 "\"zone\":\"z2\",\"min_num_replicas\":1}]}";

      createTableWithinTablespace(stmt, tablespace, table, tableSpacePlacementInfo);
      // If automatic tablet splitting is enabled (which is set to true by default)
      // and TServer's cpu size is <= 2, tablet size will be set to 1.
      assertEquals(1, getTabletsLocationsCount(table));
    }
  }

  @Test
  public void testMultiZoneWithCpu2() throws Exception {
    List<Map<String, String>> placementInfo = Arrays.asList(
        ImmutableMap.of(
            "placement_cloud", "c1",
            "placement_region", "r1",
            "placement_zone", "z1"),
        ImmutableMap.of(
            "placement_cloud", "c1",
            "placement_region", "r1",
            "placement_zone", "z2"),
        ImmutableMap.of(
            "placement_cloud", "c1",
            "placement_region", "r1",
            "placement_zone", "z3"));
    Consumer<MiniYBClusterBuilder> builder = x -> {
      x.perTServerFlags(placementInfo);
      x.numShardsPerTServer(-1);
      x.addCommonTServerFlag("num_cpus", "4");
      x.addMasterFlag("num_cpus", "1");
    };
    restartClusterWithClusterBuilder(builder);

    try (Connection conn = getConnectionBuilder().withTServer(0).connect();
         Statement stmt = conn.createStatement()) {
       String tablespace = "multi_zone_tablespace_with_multi_shards";
       String table ="multi_zone_tablespace_table_with_multi_shards";
       String tableSpacePlacementInfo =
           "{\"num_replicas\": 2, \"placement_blocks\":" +
               "[{\"cloud\":\"c1\",\"region\":\"r1\"," +
                 "\"zone\":\"z1\",\"min_num_replicas\":1}," +
                "{\"cloud\":\"c1\",\"region\":\"r1\"," +
                 "\"zone\":\"z2\",\"min_num_replicas\":1}]}";

      createTableWithinTablespace(stmt, tablespace, table, tableSpacePlacementInfo);
      // If automatic tablet splitting is enabled (which is set to true by default)
      // and TServer's cpu size is <= 4, tablet size will be the smaller of 2 and
      // actual tablet size.
      assertEquals(2, getTabletsLocationsCount(table));
    }
  }

  @Test
  public void testMultiZoneWithCpu3() throws Exception {
    List<Map<String, String>> placementInfo = Arrays.asList(
        ImmutableMap.of(
            "placement_cloud", "c1",
            "placement_region", "r1",
            "placement_zone", "z1"),
        ImmutableMap.of(
            "placement_cloud", "c1",
            "placement_region", "r1",
            "placement_zone", "z2"),
        ImmutableMap.of(
            "placement_cloud", "c1",
            "placement_region", "r1",
            "placement_zone", "z3"));
    Consumer<MiniYBClusterBuilder> builder = x -> {
      x.perTServerFlags(placementInfo);
      x.numShardsPerTServer(-1);
      x.addCommonTServerFlag("enable_automatic_tablet_splitting", "false");
      x.addCommonTServerFlag("num_cpus", "6");
      x.addMasterFlag("num_cpus", "1");
    };
    restartClusterWithClusterBuilder(builder);

    try (Connection conn = getConnectionBuilder().withTServer(0).connect();
         Statement stmt = conn.createStatement()) {
       String tablespace = "multi_zone_tablespace_with_multi_shards";
       String table ="multi_zone_tablespace_table_with_multi_shards";
       String tableSpacePlacementInfo =
           "{\"num_replicas\": 2, \"placement_blocks\":" +
               "[{\"cloud\":\"c1\",\"region\":\"r1\"," +
                 "\"zone\":\"z1\",\"min_num_replicas\":1}," +
                "{\"cloud\":\"c1\",\"region\":\"r1\"," +
                 "\"zone\":\"z2\",\"min_num_replicas\":1}]}";

      createTableWithinTablespace(stmt, tablespace, table, tableSpacePlacementInfo);
      // If automatic tablet splitting is disabled and TServer's cpu size is > 4, then
      // if it's TSAN, it will multiple 2 by TServer count which is 2, so 4 will be total tablets.
      // Else, it will multiple 8 by TServer count which is 2, so 16 will be total tablets.
      if (BuildTypeUtil.isTSAN())
        assertEquals(4, getTabletsLocationsCount(table));
      else
        assertEquals(16, getTabletsLocationsCount(table));
    }
  }
}
