// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.CommissionerBaseTest;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.ProviderDetails;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import junitparams.JUnitParamsRunner;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.client.ListMasterRaftPeersResponse;
import org.yb.client.YBClient;
import org.yb.util.PeerInfo;

@RunWith(JUnitParamsRunner.class)
public class AnsibleConfigureServerTest extends CommissionerBaseTest {
  private AvailabilityZone az;
  private Provider provider;
  private Universe universe;
  private ListMasterRaftPeersResponse mockMastersResponse;
  private YBClient mockClient;

  @Before
  public void setup() {
    defaultCustomer = ModelFactory.testCustomer();
    setupUniverse(Common.CloudType.onprem);
    mockMastersResponse = mock(ListMasterRaftPeersResponse.class);
    mockClient = mock(YBClient.class);
    when(mockYBClient.getUniverseClient(any())).thenReturn(mockClient);
    List<PeerInfo> servers = new ArrayList<>();
    // IP for host-n1.
    PeerInfo peerInfo = new PeerInfo();
    peerInfo.setLastKnownPrivateIps(
        Collections.singletonList(HostAndPort.fromParts("10.0.0.1", 9070)));
    peerInfo.setMemberType(PeerInfo.MemberType.VOTER);
    servers.add(peerInfo);
    try {
      when(mockNodeManager.nodeCommand(any(), any())).thenReturn(ShellResponse.create(0, ""));
      when(mockClient.listMasterRaftPeers()).thenReturn(mockMastersResponse);
    } catch (Exception e) {
      fail();
    }
    when(mockMastersResponse.getPeersList()).thenReturn(servers);
    NodeAgent nodeAgent = mock(NodeAgent.class);
    when(mockNodeAgentClient.getAndUpgradeOrThrow(any())).thenReturn(nodeAgent);
  }

  private void setupUniverse(Common.CloudType cloudType) {
    AccessKey.KeyInfo keyInfo = new AccessKey.KeyInfo();
    provider = ModelFactory.newProvider(defaultCustomer, cloudType);
    provider.setDetails(new ProviderDetails());
    provider.getDetails().mergeFrom(keyInfo);
    provider.save();
    Region r = Region.create(provider, "r-1", "r-1", "yb-image");
    AccessKey.create(provider.getUuid(), "demo-key", keyInfo);
    az = AvailabilityZone.createOrThrow(r, "az-1", "az-1", "subnet-1");
    universe =
        ModelFactory.createUniverse(
            cloudType.name() + "-universe", defaultCustomer.getId(), cloudType);
    // Save the updates to the universe.
    Universe.saveDetails(universe.getUniverseUUID(), ApiUtils.mockUniverseUpdater());
  }

  @Test
  public void testNoResetMasterStateInMaster() {
    AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
    params.azUuid = az.getUuid();
    params.setUniverseUUID(universe.getUniverseUUID());
    params.nodeName = "host-n1";
    params.resetMasterState = true;
    params.isMasterInShellMode = true;
    params.deviceInfo = ApiUtils.getDummyDeviceInfo(1, 100);
    params.setProperty("processType", ServerType.MASTER.name().toLowerCase());
    AnsibleConfigureServers ansibleConfigServer =
        AbstractTaskBase.createTask(AnsibleConfigureServers.class);
    ansibleConfigServer.initialize(params);
    ansibleConfigServer.run();
    verify(mockNodeManager, times(1)).nodeCommand(NodeManager.NodeCommandType.Configure, params);
    assertFalse(params.resetMasterState);
  }

  @Test
  public void testResetMasterState() {
    AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
    params.azUuid = az.getUuid();
    params.setUniverseUUID(universe.getUniverseUUID());
    params.nodeName = "host-n2";
    params.resetMasterState = true;
    params.isMasterInShellMode = true;
    params.deviceInfo = ApiUtils.getDummyDeviceInfo(1, 100);
    params.setProperty("processType", ServerType.MASTER.name().toLowerCase());
    AnsibleConfigureServers ansibleConfigServer =
        AbstractTaskBase.createTask(AnsibleConfigureServers.class);
    ansibleConfigServer.initialize(params);
    ansibleConfigServer.run();
    verify(mockNodeManager, times(1)).nodeCommand(NodeManager.NodeCommandType.Configure, params);
    assertTrue(params.resetMasterState);
  }

  @Test
  public void testNoResetMasterStateNonShellMode() {
    AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
    params.azUuid = az.getUuid();
    params.setUniverseUUID(universe.getUniverseUUID());
    params.nodeName = "host-n2";
    params.resetMasterState = true;
    params.isMasterInShellMode = false;
    params.deviceInfo = ApiUtils.getDummyDeviceInfo(1, 100);
    params.setProperty("processType", ServerType.MASTER.name().toLowerCase());
    AnsibleConfigureServers ansibleConfigServer =
        AbstractTaskBase.createTask(AnsibleConfigureServers.class);
    ansibleConfigServer.initialize(params);
    ansibleConfigServer.run();
    verify(mockNodeManager, times(1)).nodeCommand(NodeManager.NodeCommandType.Configure, params);
    assertFalse(params.resetMasterState);
  }
}
