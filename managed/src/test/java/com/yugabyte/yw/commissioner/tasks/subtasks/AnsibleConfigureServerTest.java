// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.ProviderDetails;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;
import junitparams.JUnitParamsRunner;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.client.ListMastersResponse;
import org.yb.util.ServerInfo;

@RunWith(JUnitParamsRunner.class)
public class AnsibleConfigureServerTest extends FakeDBApplication {
  private Customer defaultCustomer;
  private AvailabilityZone az;
  private Provider provider;
  private Universe universe;
  private ListMastersResponse mockMastersResponse;

  @Before
  public void setup() {
    defaultCustomer = ModelFactory.testCustomer();
    setupUniverse(Common.CloudType.onprem);
    mockMastersResponse = mock(ListMastersResponse.class);
    when(mockService.getClient(any(), any())).thenReturn(mockYBClient);
    List<ServerInfo> servers = new ArrayList<>();
    // IP for host-n1.
    servers.add(new ServerInfo(UUID.randomUUID().toString(), "10.0.0.1", 9070, false, "NONE"));
    try {
      when(mockNodeManager.nodeCommand(any(), any())).thenReturn(ShellResponse.create(0, ""));
      when(mockYBClient.listMasters()).thenReturn(mockMastersResponse);
    } catch (Exception e) {
      fail();
    }
    when(mockMastersResponse.getMasters()).thenReturn(servers);
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
    params.setProperty("processType", ServerType.MASTER.name().toLowerCase());
    AnsibleConfigureServers ansibleConfigServer =
        AbstractTaskBase.createTask(AnsibleConfigureServers.class);
    ansibleConfigServer.initialize(params);
    ansibleConfigServer.run();
    verify(mockNodeManager, times(1)).nodeCommand(NodeManager.NodeCommandType.Configure, params);
    assertFalse(params.resetMasterState);
  }
}
