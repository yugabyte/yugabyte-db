// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.common;

import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleConfigureServers;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleDestroyServer;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleSetupServer;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleUpdateNodeInfo;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleClusterServerCtl;
import com.yugabyte.yw.models.*;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.runners.MockitoJUnitRunner;


import static org.hamcrest.CoreMatchers.*;
import static org.junit.Assert.assertThat;
import static org.mockito.Mockito.when;

@RunWith(MockitoJUnitRunner.class)
public class DevOpsHelperTest extends FakeDBApplication {

  @Mock
  play.Configuration mockAppConfig;

  @InjectMocks
  DevOpsHelper devOpsHelper;

  private AvailabilityZone defaultAZ;
  private Region defaultRegion;
  private Provider defaultProvider;
  private String baseCommand;

  @Before
  public void setUp() {
    defaultProvider = Provider.create(Common.CloudType.aws.toString(), "Amazon");
    defaultRegion = Region.create(defaultProvider, "region-1", "Region 1", "yb-image-1");
    defaultAZ = AvailabilityZone.create(defaultRegion, "az-1", "AZ 1", "subnet-1");
    when(mockAppConfig.getString("yb.devops.home")).thenReturn("/my/devops");
    baseCommand = "/my/devops/bin/ybcloud.sh " + defaultProvider.code +
      " --region " + defaultRegion.code;
  }

  @Test
  public void testProvisionNodeCommandWithInvalidParam() {
    NodeTaskParams params = new NodeTaskParams();
    params.cloud = Common.CloudType.aws;
    params.azUuid = defaultAZ.uuid;

    try {
      devOpsHelper.nodeCommand(DevOpsHelper.NodeCommandType.Provision, params);
    } catch (RuntimeException re) {
      assertThat(re.getMessage(), is("NodeTaskParams is not AnsibleSetupServer.Params"));
    }
  }

  @Test
  public void testProvisionNodeCommand() {
    AnsibleSetupServer.Params params = new AnsibleSetupServer.Params();
    params.cloud = Common.CloudType.aws;
    params.azUuid = defaultAZ.uuid;
    params.subnetId = "subnet-11";
    params.instanceType = "c3.xlarge";
    params.nodeName = "foo";

    String command = devOpsHelper.nodeCommand(DevOpsHelper.NodeCommandType.Provision, params);
    String expectedCommand = baseCommand + " instance provision" +
      " --cloud_subnet " + params.subnetId + " --machine_image " + defaultRegion.ybImage +
      " --instance_type " + params.instanceType + " --assign_public_ip --reuse_host " + params.nodeName;

    assertThat(command, allOf(notNullValue(), equalTo(expectedCommand)));
  }


  @Test
  public void testConfigureNodeCommandWithInvalidParam() {
    NodeTaskParams params = new NodeTaskParams();
    params.cloud = Common.CloudType.aws;
    params.azUuid = defaultAZ.uuid;

    try {
      devOpsHelper.nodeCommand(DevOpsHelper.NodeCommandType.Configure, params);
    } catch (RuntimeException re) {
      assertThat(re.getMessage(), is("NodeTaskParams is not AnsibleConfigureServers.Params"));
    }
  }

  @Test
  public void testConfigureNodeCommand() {
    Universe u = Universe.create("Test universe", 1L);
    u = Universe.saveDetails(u.universeUUID, ApiUtils.mockUniverseUpdater());
    AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
    params.cloud = Common.CloudType.aws;
    params.azUuid = defaultAZ.uuid;
    params.nodeName = "foo";
    params.isMasterInShellMode = false;
    params.ybServerPkg = "yb-server-pkg";
    params.universeUUID = u.universeUUID;

    String command = devOpsHelper.nodeCommand(DevOpsHelper.NodeCommandType.Configure, params);
    String expectedCommand = baseCommand + " instance configure" +
      " --package " + params.ybServerPkg + " --master_addresses_for_tserver ";

    assertThat(command, allOf(notNullValue(), startsWith(expectedCommand)));
  }

  @Test
  public void testConfigureNodeCommandInShellMode() {
    Universe u = Universe.create("Test universe", 1L);
    u = Universe.saveDetails(u.universeUUID, ApiUtils.mockUniverseUpdater());
    AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
    params.cloud = Common.CloudType.aws;
    params.azUuid = defaultAZ.uuid;
    params.nodeName = "foo";
    params.isMasterInShellMode = true;
    params.ybServerPkg = "yb-server-pkg";
    params.universeUUID = u.universeUUID;

    String command = devOpsHelper.nodeCommand(DevOpsHelper.NodeCommandType.Configure, params);
    String expectedCommand = baseCommand + " instance configure" +
      " --package " + params.ybServerPkg;

    assertThat(command, allOf(notNullValue(), startsWith(expectedCommand)));
  }

  @Test
  public void testDestroyNodeCommandWithInvalidParam() {
    NodeTaskParams params = new NodeTaskParams();
    params.cloud = Common.CloudType.aws;
    params.azUuid = defaultAZ.uuid;

    try {
      devOpsHelper.nodeCommand(DevOpsHelper.NodeCommandType.Destroy, params);
    } catch (RuntimeException re) {
      assertThat(re.getMessage(), is("NodeTaskParams is not AnsibleDestroyServer.Params"));
    }
  }

  @Test
  public void testDestroyNodeCommand() {
    AnsibleDestroyServer.Params params = new AnsibleDestroyServer.Params();
    params.cloud = Common.CloudType.aws;
    params.azUuid = defaultAZ.uuid;
    params.nodeName = "foo";

    String command = devOpsHelper.nodeCommand(DevOpsHelper.NodeCommandType.Destroy, params);
    String expectedCommand = baseCommand + " instance destroy " + params.nodeName;
    assertThat(command, allOf(notNullValue(), equalTo(expectedCommand)));
  }

  @Test
  public void testListNodeCommandWithInvalidParam() {
    NodeTaskParams params = new NodeTaskParams();
    params.cloud = Common.CloudType.aws;
    params.azUuid = defaultAZ.uuid;

    try {
      devOpsHelper.nodeCommand(DevOpsHelper.NodeCommandType.List, params);
    } catch (RuntimeException re) {
      assertThat(re.getMessage(), is("NodeTaskParams is not AnsibleUpdateNodeInfo.Params"));
    }
  }

  @Test
  public void testListNodeCommand() {
    AnsibleUpdateNodeInfo.Params params = new AnsibleUpdateNodeInfo.Params();
    params.cloud = Common.CloudType.aws;
    params.azUuid = defaultAZ.uuid;
    params.nodeName = "foo";

    String command = devOpsHelper.nodeCommand(DevOpsHelper.NodeCommandType.List, params);
    String expectedCommand = baseCommand + " instance list --as_json " +  params.nodeName;
    assertThat(command, allOf(notNullValue(), equalTo(expectedCommand)));
  }

  @Test
  public void testControlNodeCommandWithInvalidParam() {
    NodeTaskParams params = new NodeTaskParams();
    params.cloud = Common.CloudType.aws;
    params.azUuid = defaultAZ.uuid;

    try {
      devOpsHelper.nodeCommand(DevOpsHelper.NodeCommandType.Control, params);
    } catch (RuntimeException re) {
      assertThat(re.getMessage(), is("NodeTaskParams is not AnsibleClusterServerCtl.Params"));
    }
  }

  @Test
  public void testControlNodeCommand() {
    AnsibleClusterServerCtl.Params params = new AnsibleClusterServerCtl.Params();
    params.cloud = Common.CloudType.aws;
    params.azUuid = defaultAZ.uuid;
    params.nodeName = "foo";
    params.process = "master";
    params.command = "create";

    String command = devOpsHelper.nodeCommand(DevOpsHelper.NodeCommandType.Control, params);
    String expectedCommand = baseCommand + " instance control " +
      params.process + " " + params.command + " " +  params.nodeName;
    assertThat(command, allOf(notNullValue(), equalTo(expectedCommand)));
  }
}
