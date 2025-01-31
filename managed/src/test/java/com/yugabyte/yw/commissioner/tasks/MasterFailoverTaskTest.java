// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static org.junit.Assert.fail;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.anyLong;
import static org.mockito.Mockito.doNothing;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import com.google.common.collect.ImmutableList;
import com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.nodeui.MetricGroup;
import com.yugabyte.yw.controllers.UniverseControllerRequestBinder;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.Collections;
import java.util.HashMap;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.MockedStatic;
import org.mockito.Mockito;
import org.mockito.junit.MockitoJUnitRunner;
import org.yb.client.ListMasterRaftPeersResponse;
import org.yb.client.YBClient;
import org.yb.util.PeerInfo;

@RunWith(MockitoJUnitRunner.class)
public class MasterFailoverTaskTest extends CommissionerBaseTest {

  private MockedStatic<MetricGroup> mockedMetricGroup;
  private Universe defaultUniverse;

  @Override
  @Before
  public void setUp() {
    super.setUp();

    Region region = Region.create(defaultProvider, "region-1", "Region 1", "yb-image-1");
    AvailabilityZone.createOrThrow(region, "az-1", "AZ 1", "subnet-1");
    // Create default universe.
    UniverseDefinitionTaskParams.UserIntent userIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    userIntent.provider = defaultProvider.getUuid().toString();
    userIntent.numNodes = 4;
    userIntent.ybSoftwareVersion = "yb-version";
    userIntent.accessKeyCode = "demo-access";
    userIntent.regionList = ImmutableList.of(region.getUuid());
    defaultUniverse = createUniverse(defaultCustomer.getId());

    PlacementInfo placementInfo =
        PlacementInfoUtil.getPlacementInfo(
            ClusterType.PRIMARY, userIntent, 3, null, Collections.emptyList());
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            ApiUtils.mockUniverseUpdater(userIntent, "host", true, false, placementInfo, true));
    // Change the zone of all nodes to the same so that we dont face any problems when finding a
    // master replacement.
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            (universe -> {
              universe
                  .getUniverseDetails()
                  .nodeDetailsSet
                  .forEach(
                      node -> {
                        node.cloudInfo.az = "az-1";
                      });
            }));
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());

    when(mockNodeManager.nodeCommand(any(), any()))
        .then(
            invocation -> {
              if (invocation.getArgument(0).equals(NodeManager.NodeCommandType.List)) {
                ShellResponse listResponse = new ShellResponse();
                NodeTaskParams params = invocation.getArgument(1);
                if (params.nodeUuid == null) {
                  listResponse.message = "{\"universe_uuid\":\"" + params.getUniverseUUID() + "\"}";
                } else {
                  listResponse.message =
                      "{\"universe_uuid\":\""
                          + params.getUniverseUUID()
                          + "\", "
                          + "\"node_uuid\": \""
                          + params.nodeUuid
                          + "\"}";
                }
                return listResponse;
              }
              return ShellResponse.create(ShellResponse.ERROR_CODE_SUCCESS, "true");
            });
    when(mockNodeUniverseManager.runCommand(any(), any(), any()))
        .thenReturn(ShellResponse.create(ShellResponse.ERROR_CODE_SUCCESS, "true"));
    ListMasterRaftPeersResponse listMastersResponse = mock(ListMasterRaftPeersResponse.class);
    PeerInfo peerInfo = new PeerInfo();
    peerInfo.setLastKnownPrivateIps(
        Collections.singletonList(HostAndPort.fromParts("10.0.0.4", 7000)));
    peerInfo.setMemberType(PeerInfo.MemberType.VOTER);
    mockedMetricGroup = Mockito.mockStatic(MetricGroup.class);
    mockedMetricGroup
        .when(() -> MetricGroup.getTabletFollowerLagMap(any()))
        .thenReturn(new HashMap<>());

    YBClient mockClient = mock(YBClient.class);
    try {
      when(mockClient.listMasterRaftPeers()).thenReturn(listMastersResponse);
      when(listMastersResponse.getPeersList()).thenReturn(Collections.singletonList(peerInfo));
      when(mockClient.setFlag(any(), any(), any(), anyBoolean())).thenReturn(true);
      doNothing().when(mockClient).waitForMasterLeader(anyLong());
      when(mockClient.waitForMaster(any(), anyLong())).thenReturn(true);
      when(mockClient.getLeaderMasterHostAndPort()).thenReturn(HostAndPort.fromHost("10.0.0.1"));
      when(mockYBClient.getClientWithConfig(any())).thenReturn(mockClient);
      when(mockYBClient.getClient(any(), any())).thenReturn(mockClient);
    } catch (Exception e) {
      fail();
    }
    super.setLeaderlessTabletsMock();
    super.setFollowerLagMock();
    super.setMockLiveTabletServers(mockClient, defaultUniverse);
  }

  @Test
  public void testMasterFailoverTaskRetries() {
    NodeTaskParams taskParams =
        UniverseControllerRequestBinder.deepCopy(
            defaultUniverse.getUniverseDetails(), NodeTaskParams.class);
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.nodeName = "host-n1";
    taskParams.azUuid = UUID.randomUUID();
    super.verifyTaskRetries(
        defaultCustomer,
        CustomerTask.TaskType.MasterFailover,
        CustomerTask.TargetType.Universe,
        defaultUniverse.getUniverseUUID(),
        TaskType.MasterFailover,
        taskParams);
  }
}
