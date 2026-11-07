// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.common.ApiUtils.getTestUserIntent;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.lenient;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.google.common.collect.ImmutableMap;
import com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.XClusterConfigTaskParams;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.List;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import org.yb.client.SetUniverseReplicationEnabledResponse;
import org.yb.client.YBClientApi;

@RunWith(MockitoJUnitRunner.class)
public class PauseXClusterUniversesTest extends CommissionerBaseTest {

  private static final String USER_PG_CONF_CSV = "log_statement=ddl,log_duration=on";
  private static final int NUM_NODES = 3;

  private Universe sourceUniverse;
  private Universe targetUniverse;
  private YBClientApi mockClient;

  @Before
  public void setUp() throws Exception {
    when(mockNodeManager.nodeCommand(any(), any()))
        .then(
            invocation -> {
              if (invocation.getArgument(0).equals(NodeManager.NodeCommandType.List)) {
                NodeTaskParams params = invocation.getArgument(1);
                ShellResponse listResponse = new ShellResponse();
                listResponse.message =
                    "{\"universe_uuid\":\""
                        + params.getUniverseUUID()
                        + "\", \"node_uuid\": \""
                        + params.nodeUuid
                        + "\"}";
                return listResponse;
              }
              return ShellResponse.create(ShellResponse.ERROR_CODE_SUCCESS, "true");
            });
    mockClient = mock(YBClientApi.class);
    lenient().when(mockYBClient.getUniverseClient(any())).thenReturn(mockClient);
    lenient().when(mockYBClient.getClientWithConfig(any())).thenReturn(mockClient);
    lenient().when(mockYBClient.getClient(any(), any())).thenReturn(mockClient);
    when(mockClient.setFlag(any(), anyString(), anyString(), anyBoolean())).thenReturn(true);
    when(mockClient.setUniverseReplicationEnabled(any(), eq(false)))
        .thenReturn(new SetUniverseReplicationEnabledResponse(0, "", null));

    Region region = Region.create(defaultProvider, "region-1", "Region 1", "default-image");
    AvailabilityZone.createOrThrow(region, "az-1", "AZ 1", "subnet-1");
    InstanceType instanceType =
        InstanceType.upsert(
            defaultProvider.getUuid(),
            "c3.xlarge",
            10,
            5.5,
            new InstanceType.InstanceTypeDetails());
    sourceUniverse = createTestUniverse(region, instanceType, "source-universe", true);
    targetUniverse = createTestUniverse(region, instanceType, "target-universe", false);
  }

  private Universe createTestUniverse(
      Region region, InstanceType instanceType, String name, boolean withUserPgConfCsv) {
    UserIntent userIntent = getTestUserIntent(region, defaultProvider, instanceType, NUM_NODES);
    userIntent.replicationFactor = NUM_NODES;
    userIntent.universeName = name;
    if (withUserPgConfCsv) {
      userIntent.specificGFlags =
          SpecificGFlags.construct(
              ImmutableMap.of(), ImmutableMap.of(GFlagsUtil.YSQL_PG_CONF_CSV, USER_PG_CONF_CSV));
    }
    Universe universe = createUniverse(name, defaultCustomer.getId());
    return Universe.saveDetails(
        universe.getUniverseUUID(),
        ApiUtils.mockUniverseUpdater(userIntent, name, true /* setMasters */, false));
  }

  @Test
  public void testPauseKeepsUserYsqlPgConfCsvInMemory() throws Exception {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(
            "test-config", sourceUniverse.getUniverseUUID(), targetUniverse.getUniverseUUID());
    UUID taskUUID =
        commissioner.submit(
            TaskType.PauseXClusterUniverses, new XClusterConfigTaskParams(xClusterConfig));
    TaskInfo taskInfo = waitForTask(taskUUID);
    assertEquals(Success, taskInfo.getTaskState());

    // The WAL retention step sets log_min_seconds_to_retain in memory on each source tserver. It
    // must not drop the user ysql_pg_conf_csv from them.
    List<NodeDetails> sourceTservers = sourceUniverse.getTServersInPrimaryCluster();
    assertEquals(NUM_NODES, sourceTservers.size());
    for (NodeDetails node : sourceTservers) {
      HostAndPort hostPort = HostAndPort.fromParts(node.cloudInfo.private_ip, node.tserverRpcPort);
      verify(mockClient)
          .setFlag(
              eq(hostPort),
              eq(GFlagsUtil.LOG_MIN_SECONDS_TO_RETAIN),
              eq(Integer.toString(Integer.MAX_VALUE)),
              eq(true));
      verify(mockClient)
          .setFlag(eq(hostPort), eq(GFlagsUtil.YSQL_PG_CONF_CSV), eq(USER_PG_CONF_CSV), eq(true));
    }
    verify(mockClient, times(NUM_NODES))
        .setFlag(any(), eq(GFlagsUtil.YSQL_PG_CONF_CSV), anyString(), anyBoolean());
  }
}
