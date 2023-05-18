// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.ProviderDetails;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(JUnitParamsRunner.class)
public class AnsibleSetupServerTest extends NodeTaskBaseTest {
  private AnsibleSetupServer.Params createUniverse(
      Common.CloudType cloudType, AccessKey.KeyInfo accessKeyInfo) {
    Provider p = ModelFactory.newProvider(defaultCustomer, cloudType);
    p.setDetails(new ProviderDetails());
    p.getDetails().mergeFrom(accessKeyInfo);
    p.save();
    Region r = Region.create(p, "r-1", "r-1", "yb-image");
    AccessKey.create(p.getUuid(), "demo-key", accessKeyInfo);
    AvailabilityZone az = AvailabilityZone.createOrThrow(r, "az-1", "az-1", "subnet-1");
    Universe u =
        ModelFactory.createUniverse(
            cloudType.name() + "-universe", defaultCustomer.getId(), cloudType);
    // Save the updates to the universe.
    Universe.saveDetails(u.getUniverseUUID(), ApiUtils.mockUniverseUpdater());
    AnsibleSetupServer.Params params = new AnsibleSetupServer.Params();
    params.azUuid = az.getUuid();
    params.setUniverseUUID(u.getUniverseUUID());
    return params;
  }

  @Test
  public void testOnPremProviderWithAirGapOption() {
    when(mockNodeManager.nodeCommand(any(), any())).thenReturn(ShellResponse.create(0, ""));
    AnsibleSetupServer ansibleSetupServer = AbstractTaskBase.createTask(AnsibleSetupServer.class);
    AccessKey.KeyInfo keyInfo = new AccessKey.KeyInfo();
    keyInfo.airGapInstall = true;
    AnsibleSetupServer.Params params = createUniverse(Common.CloudType.onprem, keyInfo);
    ansibleSetupServer.initialize(params);
    ansibleSetupServer.run();
    verify(mockNodeManager, times(1)).nodeCommand(NodeManager.NodeCommandType.Provision, params);
  }

  @Test
  public void testOnPremProviderWithPasswordlessOptionDisabled() {
    when(mockNodeManager.nodeCommand(any(), any())).thenReturn(ShellResponse.create(0, ""));
    AnsibleSetupServer ansibleSetupServer = AbstractTaskBase.createTask(AnsibleSetupServer.class);
    AccessKey.KeyInfo keyInfo = new AccessKey.KeyInfo();
    keyInfo.passwordlessSudoAccess = false;
    AnsibleSetupServer.Params params = createUniverse(Common.CloudType.onprem, keyInfo);
    ansibleSetupServer.initialize(params);
    ansibleSetupServer.run();
    verify(mockNodeManager, times(1)).nodeCommand(NodeManager.NodeCommandType.Provision, params);
  }

  @Test
  public void testOnPremProviderWithPasswordlessOptionEnabled() {
    when(mockNodeManager.nodeCommand(any(), any())).thenReturn(ShellResponse.create(0, ""));
    AnsibleSetupServer ansibleSetupServer = AbstractTaskBase.createTask(AnsibleSetupServer.class);
    AccessKey.KeyInfo keyInfo = new AccessKey.KeyInfo();
    keyInfo.passwordlessSudoAccess = true;
    AnsibleSetupServer.Params params = createUniverse(Common.CloudType.onprem, keyInfo);
    ansibleSetupServer.initialize(params);
    ansibleSetupServer.run();
    verify(mockNodeManager, times(1)).nodeCommand(NodeManager.NodeCommandType.Provision, params);
  }

  @Test
  public void testOnPremProviderWithSkipProvision() {
    when(mockNodeManager.nodeCommand(any(), any())).thenReturn(ShellResponse.create(0, ""));
    AnsibleSetupServer ansibleSetupServer = AbstractTaskBase.createTask(AnsibleSetupServer.class);
    AccessKey.KeyInfo keyInfo = new AccessKey.KeyInfo();
    keyInfo.skipProvisioning = true;
    AnsibleSetupServer.Params params = createUniverse(Common.CloudType.onprem, keyInfo);
    ansibleSetupServer.initialize(params);
    ansibleSetupServer.run();
    verify(mockNodeManager, times(0)).nodeCommand(NodeManager.NodeCommandType.Provision, params);
  }

  @Test
  public void testOnPremProviderWithoutSkipProvision() {
    when(mockNodeManager.nodeCommand(any(), any())).thenReturn(ShellResponse.create(0, ""));
    AnsibleSetupServer ansibleSetupServer = AbstractTaskBase.createTask(AnsibleSetupServer.class);
    AccessKey.KeyInfo keyInfo = new AccessKey.KeyInfo();
    keyInfo.skipProvisioning = false;
    AnsibleSetupServer.Params params = createUniverse(Common.CloudType.onprem, keyInfo);
    ansibleSetupServer.initialize(params);
    ansibleSetupServer.run();
    verify(mockNodeManager, times(1)).nodeCommand(NodeManager.NodeCommandType.Provision, params);
  }

  @Test
  public void testOnPremProviderWithMultipleAccessKeys() {
    when(mockNodeManager.nodeCommand(any(), any())).thenReturn(ShellResponse.create(0, ""));
    AnsibleSetupServer ansibleSetupServer = AbstractTaskBase.createTask(AnsibleSetupServer.class);
    AccessKey.KeyInfo keyInfo = new AccessKey.KeyInfo();
    AnsibleSetupServer.Params params = createUniverse(Common.CloudType.onprem, keyInfo);
    AccessKey.create(params.getProvider().getUuid(), "demo-key-2", keyInfo);
    ansibleSetupServer.initialize(params);
    ansibleSetupServer.run();
    verify(mockNodeManager, times(1)).nodeCommand(NodeManager.NodeCommandType.Provision, params);
  }

  @Test
  @Parameters({"aws", "gcp", "azu", "kubernetes", "onprem"})
  public void testAllProvidersWithAccessKey(String code) {
    Common.CloudType cloudType = Common.CloudType.valueOf(code);
    when(mockNodeManager.nodeCommand(any(), any())).thenReturn(ShellResponse.create(0, ""));
    AnsibleSetupServer ansibleSetupServer = AbstractTaskBase.createTask(AnsibleSetupServer.class);
    AccessKey.KeyInfo keyInfo = new AccessKey.KeyInfo();
    AnsibleSetupServer.Params params = createUniverse(cloudType, keyInfo);
    ansibleSetupServer.initialize(params);
    ansibleSetupServer.run();
    verify(mockNodeManager, times(1)).nodeCommand(NodeManager.NodeCommandType.Provision, params);
  }
}
