// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.common;

import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase;
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
import play.libs.Json;

import java.util.HashMap;
import java.util.UUID;

import static com.yugabyte.yw.commissioner.tasks.UpgradeUniverse.UpgradeTaskSubType.Download;
import static com.yugabyte.yw.commissioner.tasks.UpgradeUniverse.UpgradeTaskSubType.Install;
import static com.yugabyte.yw.commissioner.tasks.UpgradeUniverse.UpgradeTaskType.GFlags;
import static com.yugabyte.yw.commissioner.tasks.UpgradeUniverse.UpgradeTaskType.Software;
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
  private Customer defaultCustomer;


  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    defaultProvider = ModelFactory.awsProvider(defaultCustomer);
    defaultRegion = Region.create(defaultProvider, "region-1", "Region 1", "yb-image-1");
    defaultAZ = AvailabilityZone.create(defaultRegion, "az-1", "AZ 1", "subnet-1");
    when(mockAppConfig.getString("yb.devops.home")).thenReturn("/my/devops");
    baseCommand = "/my/devops/bin/ybcloud.sh " + defaultProvider.code +
      " --zone " + defaultAZ.code +  " --region " + defaultRegion.code;
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
      " --instance_type " + params.instanceType + " --assign_public_ip " + params.nodeName;

    assertThat(command, allOf(notNullValue(), equalTo(expectedCommand)));
  }


  @Test
  public void testConfigureNodeCommandWithInvalidParam() {
    Universe u = Universe.create("Test universe", UUID.randomUUID(), 1L);
    NodeTaskParams params = new NodeTaskParams();
    params.cloud = Common.CloudType.aws;
    params.azUuid = defaultAZ.uuid;
    params.universeUUID = u.universeUUID;

    try {
      devOpsHelper.nodeCommand(DevOpsHelper.NodeCommandType.Configure, params);
    } catch (RuntimeException re) {
      assertThat(re.getMessage(), is("NodeTaskParams is not AnsibleConfigureServers.Params"));
    }
  }

  @Test
  public void testConfigureNodeCommand() {
    Universe u = Universe.create("Test universe", UUID.randomUUID(), 1L);
    u = Universe.saveDetails(u.universeUUID, ApiUtils.mockUniverseUpdater());
    AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
    params.cloud = Common.CloudType.aws;
    params.azUuid = defaultAZ.uuid;
    params.nodeName = "foo";
    params.isMasterInShellMode = true;
    params.ybServerPackage = "yb-server-pkg";
    params.universeUUID = u.universeUUID;

    String command = devOpsHelper.nodeCommand(DevOpsHelper.NodeCommandType.Configure, params);

    String expectedCommand = baseCommand + " instance configure" +
      " --master_addresses_for_tserver (.*)(|:)([0-9]*)" +
      " --package " + params.ybServerPackage + " " + params.nodeName;

    assertThat(command, allOf(notNullValue(), RegexMatcher.matchesRegex(expectedCommand)));
  }

  @Test
  public void testConfigureNodeCommandInShellMode() {
    Universe u = Universe.create("Test universe", UUID.randomUUID(), 1L);
    u = Universe.saveDetails(u.universeUUID, ApiUtils.mockUniverseUpdater());
    AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
    params.cloud = Common.CloudType.aws;
    params.azUuid = defaultAZ.uuid;
    params.nodeName = "foo";
    params.isMasterInShellMode = false;
    params.ybServerPackage = "yb-server-pkg";
    params.universeUUID = u.universeUUID;

    String command = devOpsHelper.nodeCommand(DevOpsHelper.NodeCommandType.Configure, params);

    String expectedCommand = baseCommand + " instance configure" +
      " --master_addresses_for_tserver (.*)(|:)([0-9]*)" +
      " --master_addresses_for_master (.*)(|:)([0-9]*)" +
      " --package " + params.ybServerPackage + " " + params.nodeName;

    assertThat(command, allOf(notNullValue(), RegexMatcher.matchesRegex(expectedCommand)));
  }


  @Test
  public void testSoftwareUpgradeWithoutRequiredProperties() {
    Universe u = Universe.create("Test universe", UUID.randomUUID(), 1L);
    u = Universe.saveDetails(u.universeUUID, ApiUtils.mockUniverseUpdater());
    AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
    params.cloud = Common.CloudType.aws;
    params.azUuid = defaultAZ.uuid;
    params.nodeName = "foo";
    params.ybServerPackage = "yb-server-pkg";
    params.type = Software;
    params.universeUUID = u.universeUUID;

    try {
      devOpsHelper.nodeCommand(DevOpsHelper.NodeCommandType.Configure, params);

    } catch (RuntimeException re) {
      assertThat(re.getMessage(), allOf(notNullValue(), is("Invalid taskSubType property: null")));
    }
  }

  @Test
  public void testSoftwareUpgradeWithDownloadNodeCommand() {
    Universe u = Universe.create("Test universe", UUID.randomUUID(), 1L);
    u = Universe.saveDetails(u.universeUUID, ApiUtils.mockUniverseUpdater());
    AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
    params.cloud = Common.CloudType.aws;
    params.azUuid = defaultAZ.uuid;
    params.nodeName = "foo";
    params.ybServerPackage = "yb-server-pkg";
    params.type = Software;
    params.setProperty("taskSubType", Download.toString());
    params.universeUUID = u.universeUUID;

    String command = devOpsHelper.nodeCommand(DevOpsHelper.NodeCommandType.Configure, params);

    String expectedCommand = baseCommand + " instance configure" +
      " --master_addresses_for_tserver (.*)(|:)([0-9]*)" +
      " --package " + params.ybServerPackage + " --tags download-software " + params.nodeName;

    assertThat(command, allOf(notNullValue(), RegexMatcher.matchesRegex(expectedCommand)));
  }

  @Test
  public void testSoftwareUpgradeWithInstallNodeCommand() {
    Universe u = Universe.create("Test universe", UUID.randomUUID(), 1L);
    u = Universe.saveDetails(u.universeUUID, ApiUtils.mockUniverseUpdater());
    AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
    params.cloud = Common.CloudType.aws;
    params.azUuid = defaultAZ.uuid;
    params.nodeName = "foo";
    params.ybServerPackage = "yb-server-pkg";
    params.type = Software;
    params.setProperty("taskSubType", Install.toString());
    params.universeUUID = u.universeUUID;

    String command = devOpsHelper.nodeCommand(DevOpsHelper.NodeCommandType.Configure, params);

    String expectedCommand = baseCommand + " instance configure" +
      " --master_addresses_for_tserver (.*)(|:)([0-9]*)" +
      " --package " + params.ybServerPackage + " --tags install-software " + params.nodeName;

    assertThat(command, allOf(notNullValue(), RegexMatcher.matchesRegex(expectedCommand)));
  }

  @Test
  public void testGFlagsUpgradeWithoutRequiredProperties() {
    Universe u = Universe.create("Test universe", UUID.randomUUID(), 1L);
    u = Universe.saveDetails(u.universeUUID, ApiUtils.mockUniverseUpdater());
    AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
    params.cloud = Common.CloudType.aws;
    params.azUuid = defaultAZ.uuid;
    params.nodeName = "foo";
    HashMap<String, String> gflags = new HashMap<>();
    gflags.put("gflagName", "gflagValue");
    params.gflags = gflags;
    params.type = GFlags;
    params.universeUUID = u.universeUUID;

    try {
      devOpsHelper.nodeCommand(DevOpsHelper.NodeCommandType.Configure, params);

    } catch (RuntimeException re) {
      assertThat(re.getMessage(), allOf(notNullValue(), is("Invalid processType property: null")));
    }
  }

  @Test
  public void testGFlagsUpgradeWithEmptyGFlagsNodeCommand() {
    Universe u = Universe.create("Test universe", UUID.randomUUID(), 1L);
    u = Universe.saveDetails(u.universeUUID, ApiUtils.mockUniverseUpdater());
    AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
    params.cloud = Common.CloudType.aws;
    params.azUuid = defaultAZ.uuid;
    params.nodeName = "foo";
    params.type = GFlags;
    params.setProperty("processType", UniverseDefinitionTaskBase.ServerType.MASTER.toString());
    params.universeUUID = u.universeUUID;


    try {
      devOpsHelper.nodeCommand(DevOpsHelper.NodeCommandType.Configure, params);

    } catch (RuntimeException re) {
      assertThat(re.getMessage(), allOf(notNullValue(), is("Empty GFlags data provided")));
    }
  }

  @Test
  public void testGFlagsUpgradeForMasterNodeCommand() {
    Universe u = Universe.create("Test universe", UUID.randomUUID(), 1L);
    u = Universe.saveDetails(u.universeUUID, ApiUtils.mockUniverseUpdater());
    AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
    params.cloud = Common.CloudType.aws;
    params.azUuid = defaultAZ.uuid;
    params.nodeName = "foo";
    HashMap<String, String> gflags = new HashMap<>();
    gflags.put("gflagName", "gflagValue");
    params.gflags = gflags;
    params.type = GFlags;
    params.setProperty("processType", UniverseDefinitionTaskBase.ServerType.MASTER.toString());
    params.universeUUID = u.universeUUID;

    String command = devOpsHelper.nodeCommand(DevOpsHelper.NodeCommandType.Configure, params);
    String gflagsJson =  Json.stringify(Json.toJson(gflags));

    String expectedCommand = baseCommand + " instance configure" +
      " --master_addresses_for_tserver (.*)(|:)([0-9]*)" +
      " --replace_gflags --gflags \\" + gflagsJson + "\\ " + params.nodeName;

    assertThat(command, allOf(notNullValue(), RegexMatcher.matchesRegex(expectedCommand)));
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

  @Test
  public void testDockerNodeCommandWithoutDockerNetwork() {
    AnsibleUpdateNodeInfo.Params params = new AnsibleUpdateNodeInfo.Params();
    params.cloud = Common.CloudType.docker;
    params.azUuid = defaultAZ.uuid;
    params.nodeName = "foo";
    try {
      devOpsHelper.nodeCommand(DevOpsHelper.NodeCommandType.List, params);
    } catch (RuntimeException re) {
      assertThat(re.getMessage(), allOf(notNullValue(), is("yb.docker.network is not set in application.conf")));
    }
  }

  @Test
  public void testDockerNodeCommandWithDockerNetwork() {
    when(mockAppConfig.getString("yb.docker.network")).thenReturn("yugaware_bridge");

    AnsibleUpdateNodeInfo.Params params = new AnsibleUpdateNodeInfo.Params();
    params.cloud = Common.CloudType.docker;
    params.azUuid = defaultAZ.uuid;
    params.nodeName = "foo";
    String command = devOpsHelper.nodeCommand(DevOpsHelper.NodeCommandType.List, params);
    String expectedCommand = "/my/devops/bin/ybcloud.sh " + params.cloud +
            " --zone " + defaultAZ.code +  " --region " + defaultRegion.code +
            " --network yugaware_bridge" +
            " instance list --as_json " +  params.nodeName;
    assertThat(command, allOf(notNullValue(), equalTo(expectedCommand)));
  }
}
