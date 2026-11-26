// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import static com.yugabyte.yw.common.ModelFactory.createFromConfig;
import static com.yugabyte.yw.common.ModelFactory.generateTablespaceParams;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;

import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.TableSpaceStructures.PlacementBlock;
import com.yugabyte.yw.common.TableSpaceStructures.TableSpaceInfo;
import com.yugabyte.yw.common.TableSpaceStructures.TableSpaceQueryResponse;
import com.yugabyte.yw.forms.CreateTablespaceParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.backuprestore.Tablespace;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import junitparams.JUnitParamsRunner;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;

@RunWith(JUnitParamsRunner.class)
public class TableSpaceUtilTest extends FakeDBApplication {

  @Rule public MockitoRule rule = MockitoJUnit.rule();

  private Customer customer;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
  }

  @Test
  public void testValidateTablespaces_FailFlows() {
    Provider provider = ModelFactory.newProvider(customer, CloudType.onprem);
    Universe universe = createFromConfig(provider, "Existing", "r1-az1-4-1;r1-az2-3-1;r1-az3-4-1");

    // Invalid zone.
    CreateTablespaceParams params1 =
        generateTablespaceParams(
            universe.getUniverseUUID(), provider.getCode(), 3, "r1-az1-1;r1-az2-1;r1-az4-1");
    RuntimeException re =
        assertThrows(
            PlatformServiceException.class,
            () -> TableSpaceUtil.validateTablespaces(params1, universe));
    assertEquals(
        "Invalid placement specified by cloud onprem, region r1, zone az4", re.getMessage());

    // Not enough zones.
    CreateTablespaceParams params2 =
        generateTablespaceParams(
            universe.getUniverseUUID(), provider.getCode(), 3, "r1-az1-1;r1-az2-4;r1-az3-1");
    re =
        assertThrows(
            PlatformServiceException.class,
            () -> TableSpaceUtil.validateTablespaces(params2, universe));
    assertEquals(
        "Placement in cloud onprem, region r1, zone az2 doesn't have enough nodes",
        re.getMessage());

    // Inconsistent number of replicas.
    CreateTablespaceParams params3 =
        generateTablespaceParams(
            universe.getUniverseUUID(), provider.getCode(), 4, "r1-az1-1;r1-az2-1;r1-az3-1");
    re =
        assertThrows(
            PlatformServiceException.class,
            () -> TableSpaceUtil.validateTablespaces(params3, universe));
    assertEquals("Invalid number of replicas in tablespace test_tablespace", re.getMessage());

    // Duplicate placement.
    CreateTablespaceParams params4 =
        generateTablespaceParams(
            universe.getUniverseUUID(), provider.getCode(), 4, "r1-az1-1;r1-az2-1;r1-az2-2");
    re =
        assertThrows(
            PlatformServiceException.class,
            () -> TableSpaceUtil.validateTablespaces(params4, universe));
    assertEquals("Duplicate placement for cloud onprem, region r1, zone az2", re.getMessage());

    // No information about tablespaces was found.
    re =
        assertThrows(
            PlatformServiceException.class,
            () -> TableSpaceUtil.validateTablespaces(null, universe));
    assertEquals("No information about tablespaces was found", re.getMessage());

    CreateTablespaceParams params6 = new CreateTablespaceParams();
    re =
        assertThrows(
            PlatformServiceException.class,
            () -> TableSpaceUtil.validateTablespaces(params6, universe));
    assertEquals("No information about tablespaces was found", re.getMessage());

    CreateTablespaceParams params7 = new CreateTablespaceParams();
    params7.tablespaceInfos = new ArrayList<>();
    re =
        assertThrows(
            PlatformServiceException.class,
            () -> TableSpaceUtil.validateTablespaces(params7, universe));
    assertEquals("No information about tablespaces was found", re.getMessage());
  }

  @Test
  public void testValidateTablespaces_HappyPath() {
    Provider provider = ModelFactory.newProvider(customer, CloudType.onprem);
    Universe universe = createFromConfig(provider, "Existing", "r1-az1-4-1;r1-az2-3-1;r1-az3-4-1");

    CreateTablespaceParams params =
        generateTablespaceParams(
            universe.getUniverseUUID(), provider.getCode(), 3, "r1-az1-1;r1-az2-1;r1-az3-1");
    TableSpaceUtil.validateTablespaces(params, universe);
  }

  @Test
  public void testValidateTablespaces_WithLeaderPreferences_HappyPath() {
    Provider provider = ModelFactory.newProvider(customer, CloudType.onprem);
    Universe universe = createFromConfig(provider, "Existing", "r1-az1-4-1;r1-az2-3-1;r1-az3-4-1");

    // One leader_preference missed, two others set by order. (null - 1 - 2)
    CreateTablespaceParams params =
        generateTablespaceParams(
            universe.getUniverseUUID(), provider.getCode(), 3, "r1-az1-1;r1-az2-1-1;r1-az3-1-2");
    TableSpaceUtil.validateTablespaces(params, universe);

    // Repeated values 2 - 1 - 1
    params =
        generateTablespaceParams(
            universe.getUniverseUUID(), provider.getCode(), 3, "r1-az1-1-2;r1-az2-1-1;r1-az3-1-1");
    TableSpaceUtil.validateTablespaces(params, universe);

    // 2 - 1 - 3
    params =
        generateTablespaceParams(
            universe.getUniverseUUID(), provider.getCode(), 3, "r1-az1-1-2;r1-az2-1-1;r1-az3-1-3");
    TableSpaceUtil.validateTablespaces(params, universe);
  }

  @Test
  public void testValidateTablespaces_WithLeaderPreferences_FailFlows() {
    Provider provider = ModelFactory.newProvider(customer, CloudType.onprem);
    Universe universe = createFromConfig(provider, "Existing", "r1-az1-4-1;r1-az2-3-1;r1-az3-4-1");

    // First leader_preference is 2 instead of acceptable 1. The same scenario
    // covers possible negative values. (But actually they are filtered on API
    // layer by validators)
    CreateTablespaceParams params =
        generateTablespaceParams(
            universe.getUniverseUUID(), provider.getCode(), 3, "r1-az1-1;r1-az2-1-2;r1-az3-1-2");
    RuntimeException re =
        assertThrows(
            PlatformServiceException.class,
            () -> TableSpaceUtil.validateTablespaces(params, universe));
    assertEquals("Invalid first leader preference value (should be 1)", re.getMessage());

    // Wrong leader_preference order.
    CreateTablespaceParams params2 =
        generateTablespaceParams(
            universe.getUniverseUUID(), provider.getCode(), 3, "r1-az1-1-1;r1-az2-1-1;r1-az3-1-3");
    re =
        assertThrows(
            PlatformServiceException.class,
            () -> TableSpaceUtil.validateTablespaces(params2, universe));
    assertEquals(
        "Invalid leader preferences order (current value 1, next value 3)", re.getMessage());
  }

  @Test
  public void testParseToTableSpaceInfo_WithReadReplicaPlacement_DoesNotFail() {
    TableSpaceQueryResponse response = new TableSpaceQueryResponse();
    response.tableSpaceName = "ts_with_read_replicas";
    response.tableSpaceOptions =
        Arrays.asList(
            "replica_placement={\"num_replicas\":3,\"placement_blocks\":"
                + "[{\"cloud\":\"onprem\",\"region\":\"r1\",\"zone\":\"az1\",\"min_num_replicas\":1},"
                + "{\"cloud\":\"onprem\",\"region\":\"r1\",\"zone\":\"az2\",\"min_num_replicas\":1},"
                + "{\"cloud\":\"onprem\",\"region\":\"r1\",\"zone\":\"az3\",\"min_num_replicas\":1}]}",
            "read_replica_placement={\"num_replicas\":1,\"placement_blocks\":"
                + "[{\"cloud\":\"onprem\",\"region\":\"r2\",\"zone\":\"az1\",\"min_num_replicas\":1}]}");

    // read_replica_placement should be skipped and not cause a syntax error.
    TableSpaceInfo info = TableSpaceUtil.parseToTableSpaceInfo(response);

    assertNotNull(info);
    assertEquals("ts_with_read_replicas", info.name);
    assertEquals(3, info.numReplicas);
    assertNotNull(info.placementBlocks);
    assertEquals(3, info.placementBlocks.size());
  }

  private static TableSpaceQueryResponse tablespaceResponse(String replicaPlacement) {
    TableSpaceQueryResponse response = new TableSpaceQueryResponse();
    response.tableSpaceName = "ts";
    response.tableSpaceOptions =
        Collections.singletonList(TableSpaceUtil.REPLICA_PLACEMENT_TEXT + replicaPlacement);
    return response;
  }

  @Test
  public void testParseToTableSpaceInfo_WithMaxNumReplicas() {
    TableSpaceQueryResponse response =
        tablespaceResponse(
            "{\"num_replicas\":2,\"placement_blocks\":"
                + "[{\"cloud\":\"aws\",\"region\":\"r1\",\"zone\":\"az1\","
                + "\"min_num_replicas\":1,\"max_num_replicas\":1},"
                + "{\"cloud\":\"aws\",\"region\":\"r1\",\"zone\":\"az2\","
                + "\"min_num_replicas\":1}]}");

    TableSpaceInfo info = TableSpaceUtil.parseToTableSpaceInfo(response);
    assertEquals(Integer.valueOf(1), info.placementBlocks.get(0).maxNumReplicas);
    assertNull(info.placementBlocks.get(1).maxNumReplicas);

    Tablespace tablespace = Tablespace.getTablespaceFromTablespaceQueryResponse(response);
    List<PlacementBlock> blocks = tablespace.replicaPlacement.placementBlocks;
    assertEquals(1, blocks.get(0).getPlacementBlockPB().getMaxNumReplicas());
    assertFalse(blocks.get(1).getPlacementBlockPB().hasMaxNumReplicas());
  }

  @Test
  public void testParseTablespace_IgnoresUnknownKeys() {
    TableSpaceQueryResponse response =
        tablespaceResponse(
            "{\"num_replicas\":1,\"placement_uuid\":\"abc\",\"placement_blocks\":"
                + "[{\"cloud\":\"aws\",\"region\":\"r1\",\"zone\":\"az1\","
                + "\"min_num_replicas\":1,\"leader_preferance\":1}]}");

    TableSpaceInfo info = TableSpaceUtil.parseToTableSpaceInfo(response);
    assertEquals(1, info.numReplicas);
    assertEquals(1, info.placementBlocks.size());
    assertNull(info.placementBlocks.get(0).leaderPreference);

    Tablespace tablespace = Tablespace.getTablespaceFromTablespaceQueryResponse(response);
    assertEquals(1, tablespace.replicaPlacement.placementBlocks.size());
  }

  @Test
  public void testValidateTablespaces_MaxLessThanMin() {
    Provider provider = ModelFactory.newProvider(customer, CloudType.onprem);
    Universe universe = createFromConfig(provider, "Existing", "r1-az1-4-1;r1-az2-3-1;r1-az3-4-1");

    CreateTablespaceParams params =
        generateTablespaceParams(
            universe.getUniverseUUID(), provider.getCode(), 4, "r1-az1-2;r1-az2-1;r1-az3-1");
    params.tablespaceInfos.get(0).placementBlocks.get(0).maxNumReplicas = 2;
    TableSpaceUtil.validateTablespaces(params, universe);

    params.tablespaceInfos.get(0).placementBlocks.get(0).maxNumReplicas = 1;
    RuntimeException re =
        assertThrows(
            PlatformServiceException.class,
            () -> TableSpaceUtil.validateTablespaces(params, universe));
    assertEquals(
        "Maximum replicas 1 is less than minimum replicas 2 for cloud onprem, region r1, zone az1",
        re.getMessage());
  }
}
