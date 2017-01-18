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
import com.yugabyte.yw.forms.NodeInstanceFormData;
import com.yugabyte.yw.models.*;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.runners.MockitoJUnitRunner;
import play.libs.Json;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
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

  private final String DOCKER_NETWORK = "yugaware_bridge";
  private final String MASTER_ADDRESSES = "host-n1:7100,host-n2:7100,host-n3:7100";

  private class TestData {
    public Common.CloudType cloudType;
    public Provider provider;
    public Region region;
    public AvailabilityZone zone;
    public NodeInstance node;
    public String baseCommand;

    public TestData(Customer customer, Common.CloudType cloud) {
      cloudType = cloud;
      provider = ModelFactory.newProvider(customer, cloud);
      region = Region.create(provider, "region-1", "Region 1", "yb-image-1");
      zone = AvailabilityZone.create(region, "az-1", "AZ 1", "subnet-1");

      NodeInstanceFormData formData = new NodeInstanceFormData();
      formData.ip = "fake_ip";
      formData.region = region.code;
      formData.zone = zone.code;
      formData.instanceType = "fake_instance_type";
      node = NodeInstance.create(zone.uuid, formData);
      // Update name.
      node.setNodeName("fake_name:" + provider.code);
      node.save();

      baseCommand = "/my/devops/bin/ybcloud.sh " + provider.code +
        " --zone " + zone.code +  " --region " + region.code;

      if (cloudType == Common.CloudType.docker) {
        baseCommand += " --network " + DOCKER_NETWORK;
      }

      if (cloudType == Common.CloudType.onprem) {
        baseCommand += " --node_metadata " + node.getDetailsJson();
      }
    }
  }

  private List<TestData> testData;

  private Universe createUniverse() {
    UUID uuid = UUID.randomUUID();
    return Universe.create("Test universe " + uuid.toString(), uuid, 1L);
  }

  @Before
  public void setUp() {
    Customer customer = ModelFactory.testCustomer();
    testData = new ArrayList<TestData>();
    testData.add(new TestData(customer, Common.CloudType.aws));
    testData.add(new TestData(customer, Common.CloudType.onprem));
    when(mockAppConfig.getString("yb.devops.home")).thenReturn("/my/devops");
  }

  @Test
  public void testProvisionNodeCommandWithInvalidParam() {
    for (TestData t : testData) {
      NodeTaskParams params = new NodeTaskParams();
      params.cloud = t.cloudType;
      params.azUuid = t.zone.uuid;
      params.nodeName = t.node.getNodeName();

      try {
        devOpsHelper.nodeCommand(DevOpsHelper.NodeCommandType.Provision, params);
      } catch (RuntimeException re) {
        assertThat(re.getMessage(), is("NodeTaskParams is not AnsibleSetupServer.Params"));
      }
    }
  }

  @Test
  public void testProvisionNodeCommand() {
    for (TestData t : testData) {
      AnsibleSetupServer.Params params = new AnsibleSetupServer.Params();
      params.cloud = t.cloudType;
      params.azUuid = t.zone.uuid;
      params.subnetId = t.zone.subnet;
      params.instanceType = t.node.instanceTypeCode;
      params.nodeName = t.node.getNodeName();

      String command = devOpsHelper.nodeCommand(DevOpsHelper.NodeCommandType.Provision, params);
      String expectedCommand = t.baseCommand + " instance provision" +
        " --instance_type " + params.instanceType;
      if (params.cloud != Common.CloudType.onprem) {
        expectedCommand += " --cloud_subnet " + params.subnetId +
          " --machine_image " + t.region.ybImage + " --assign_public_ip";
      }
      expectedCommand += " " + params.nodeName;

      assertThat(command, allOf(notNullValue(), equalTo(expectedCommand)));
    }
  }

  @Test
  public void testConfigureNodeCommandWithInvalidParam() {
    for (TestData t : testData) {
      Universe u = createUniverse();
      NodeTaskParams params = new NodeTaskParams();
      params.cloud = t.cloudType;
      params.azUuid = t.zone.uuid;
      params.nodeName = t.node.getNodeName();
      params.universeUUID = u.universeUUID;

      try {
        devOpsHelper.nodeCommand(DevOpsHelper.NodeCommandType.Configure, params);
      } catch (RuntimeException re) {
        assertThat(re.getMessage(), is("NodeTaskParams is not AnsibleConfigureServers.Params"));
      }
    }
  }

  @Test
  public void testConfigureNodeCommand() {
    for (TestData t : testData) {
      Universe u = createUniverse();
      u = Universe.saveDetails(u.universeUUID, ApiUtils.mockUniverseUpdater());
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      params.cloud = t.cloudType;
      params.azUuid = t.zone.uuid;
      params.nodeName = t.node.getNodeName();
      params.isMasterInShellMode = true;
      params.ybServerPackage = "yb-server-pkg";
      params.universeUUID = u.universeUUID;

      String command = devOpsHelper.nodeCommand(DevOpsHelper.NodeCommandType.Configure, params);

      String expectedCommand = t.baseCommand + " instance configure" +
        " --master_addresses_for_tserver " + MASTER_ADDRESSES +
        " --package " + params.ybServerPackage + " " + params.nodeName;

      assertThat(command, allOf(notNullValue(), equalTo(expectedCommand)));
    }
  }

  @Test
  public void testConfigureNodeCommandInShellMode() {
    for (TestData t : testData) {
      Universe u = createUniverse();
      u = Universe.saveDetails(u.universeUUID, ApiUtils.mockUniverseUpdater());
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      params.cloud = t.cloudType;
      params.azUuid = t.zone.uuid;
      params.nodeName = t.node.getNodeName();
      params.isMasterInShellMode = false;
      params.ybServerPackage = "yb-server-pkg";
      params.universeUUID = u.universeUUID;

      String command = devOpsHelper.nodeCommand(DevOpsHelper.NodeCommandType.Configure, params);

      String expectedCommand = t.baseCommand + " instance configure" +
        " --master_addresses_for_tserver " + MASTER_ADDRESSES +
        " --master_addresses_for_master " + MASTER_ADDRESSES +
        " --package " + params.ybServerPackage + " " + params.nodeName;

      assertThat(command, allOf(notNullValue(), equalTo(expectedCommand)));
    }
  }


  @Test
  public void testSoftwareUpgradeWithoutRequiredProperties() {
    for (TestData t : testData) {
      Universe u = createUniverse();
      u = Universe.saveDetails(u.universeUUID, ApiUtils.mockUniverseUpdater());
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      params.cloud = t.cloudType;
      params.azUuid = t.zone.uuid;
      params.nodeName = t.node.getNodeName();
      params.ybServerPackage = "yb-server-pkg";
      params.type = Software;
      params.universeUUID = u.universeUUID;

      try {
        devOpsHelper.nodeCommand(DevOpsHelper.NodeCommandType.Configure, params);

      } catch (RuntimeException re) {
        assertThat(re.getMessage(), allOf(notNullValue(), is("Invalid taskSubType property: null")));
      }
    }
  }

  @Test
  public void testSoftwareUpgradeWithDownloadNodeCommand() {
    for (TestData t : testData) {
      Universe u = createUniverse();
      u = Universe.saveDetails(u.universeUUID, ApiUtils.mockUniverseUpdater());
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      params.cloud = t.cloudType;
      params.azUuid = t.zone.uuid;
      params.nodeName = t.node.getNodeName();
      params.ybServerPackage = "yb-server-pkg";
      params.type = Software;
      params.isMasterInShellMode = true;
      params.setProperty("taskSubType", Download.toString());
      params.universeUUID = u.universeUUID;

      String command = devOpsHelper.nodeCommand(DevOpsHelper.NodeCommandType.Configure, params);

      String expectedCommand = t.baseCommand + " instance configure" +
        " --master_addresses_for_tserver " + MASTER_ADDRESSES +
        " --package " + params.ybServerPackage + " --tags download-software " + params.nodeName;

      assertThat(command, allOf(notNullValue(), equalTo(expectedCommand)));
    }
  }

  @Test
  public void testSoftwareUpgradeWithInstallNodeCommand() {
    for (TestData t : testData) {
      Universe u = createUniverse();
      u = Universe.saveDetails(u.universeUUID, ApiUtils.mockUniverseUpdater());
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      params.cloud = t.cloudType;
      params.azUuid = t.zone.uuid;
      params.nodeName = t.node.getNodeName();
      params.ybServerPackage = "yb-server-pkg";
      params.type = Software;
      params.isMasterInShellMode = true;
      params.setProperty("taskSubType", Install.toString());
      params.universeUUID = u.universeUUID;

      String command = devOpsHelper.nodeCommand(DevOpsHelper.NodeCommandType.Configure, params);

      String expectedCommand = t.baseCommand + " instance configure" +
        " --master_addresses_for_tserver " + MASTER_ADDRESSES +
        " --package " + params.ybServerPackage + " --tags install-software " + params.nodeName;

      assertThat(command, allOf(notNullValue(), equalTo(expectedCommand)));
    }
  }

  @Test
  public void testGFlagsUpgradeWithoutRequiredProperties() {
    for (TestData t : testData) {
      Universe u = createUniverse();
      u = Universe.saveDetails(u.universeUUID, ApiUtils.mockUniverseUpdater());
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      params.cloud = t.cloudType;
      params.azUuid = t.zone.uuid;
      params.nodeName = t.node.getNodeName();
      HashMap<String, String> gflags = new HashMap<>();
      gflags.put("gflagName", "gflagValue");
      params.gflags = gflags;
      params.type = GFlags;
      params.isMasterInShellMode = true;
      params.universeUUID = u.universeUUID;

      try {
        devOpsHelper.nodeCommand(DevOpsHelper.NodeCommandType.Configure, params);

      } catch (RuntimeException re) {
        assertThat(re.getMessage(), allOf(notNullValue(), is("Invalid processType property: null")));
      }
    }
  }

  @Test
  public void testGFlagsUpgradeWithEmptyGFlagsNodeCommand() {
    for (TestData t : testData) {
      Universe u = createUniverse();
      u = Universe.saveDetails(u.universeUUID, ApiUtils.mockUniverseUpdater());
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      params.cloud = t.cloudType;
      params.azUuid = t.zone.uuid;
      params.nodeName = t.node.getNodeName();
      params.type = GFlags;
      params.setProperty("processType", UniverseDefinitionTaskBase.ServerType.MASTER.toString());
      params.universeUUID = u.universeUUID;

      try {
        devOpsHelper.nodeCommand(DevOpsHelper.NodeCommandType.Configure, params);

      } catch (RuntimeException re) {
        assertThat(re.getMessage(), allOf(notNullValue(), is("Empty GFlags data provided")));
      }
    }
  }

  @Test
  public void testGFlagsUpgradeForMasterNodeCommand() {
    for (TestData t : testData) {
      Universe u = createUniverse();
      u = Universe.saveDetails(u.universeUUID, ApiUtils.mockUniverseUpdater());
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      params.cloud = t.cloudType;
      params.azUuid = t.zone.uuid;
      params.nodeName = t.node.getNodeName();
      HashMap<String, String> gflags = new HashMap<>();
      gflags.put("gflagName", "gflagValue");
      params.gflags = gflags;
      params.type = GFlags;
      params.isMasterInShellMode = true;
      params.setProperty("processType", UniverseDefinitionTaskBase.ServerType.MASTER.toString());
      params.universeUUID = u.universeUUID;

      String command = devOpsHelper.nodeCommand(DevOpsHelper.NodeCommandType.Configure, params);
      String gflagsJson =  Json.stringify(Json.toJson(gflags));

      String expectedCommand = t.baseCommand + " instance configure" +
        " --master_addresses_for_tserver " + MASTER_ADDRESSES +
        " --replace_gflags --gflags " + gflagsJson + " " + params.nodeName;

      assertThat(command, allOf(notNullValue(), equalTo(expectedCommand)));
    }
  }

  @Test
  public void testDestroyNodeCommandWithInvalidParam() {
    for (TestData t : testData) {
      NodeTaskParams params = new NodeTaskParams();
      params.cloud = t.cloudType;
      params.azUuid = t.zone.uuid;
      params.nodeName = t.node.getNodeName();

      try {
        devOpsHelper.nodeCommand(DevOpsHelper.NodeCommandType.Destroy, params);
      } catch (RuntimeException re) {
        assertThat(re.getMessage(), is("NodeTaskParams is not AnsibleDestroyServer.Params"));
      }
    }
  }

  @Test
  public void testDestroyNodeCommand() {
    for (TestData t : testData) {
      AnsibleDestroyServer.Params params = new AnsibleDestroyServer.Params();
      params.cloud = t.cloudType;
      params.azUuid = t.zone.uuid;
      params.nodeName = t.node.getNodeName();

      String command = devOpsHelper.nodeCommand(DevOpsHelper.NodeCommandType.Destroy, params);
      String expectedCommand = t.baseCommand + " instance destroy " + params.nodeName;
      assertThat(command, allOf(notNullValue(), equalTo(expectedCommand)));
    }
  }

  @Test
  public void testListNodeCommandWithInvalidParam() {
    for (TestData t : testData) {
      NodeTaskParams params = new NodeTaskParams();
      params.cloud = t.cloudType;
      params.azUuid = t.zone.uuid;
      params.nodeName = t.node.getNodeName();

      try {
        devOpsHelper.nodeCommand(DevOpsHelper.NodeCommandType.List, params);
      } catch (RuntimeException re) {
        assertThat(re.getMessage(), is("NodeTaskParams is not AnsibleUpdateNodeInfo.Params"));
      }
    }
  }

  @Test
  public void testListNodeCommand() {
    for (TestData t : testData) {
      AnsibleUpdateNodeInfo.Params params = new AnsibleUpdateNodeInfo.Params();
      params.cloud = t.cloudType;
      params.azUuid = t.zone.uuid;
      params.nodeName = t.node.getNodeName();

      String command = devOpsHelper.nodeCommand(DevOpsHelper.NodeCommandType.List, params);
      String expectedCommand = t.baseCommand + " instance list --as_json " +  params.nodeName;
      assertThat(command, allOf(notNullValue(), equalTo(expectedCommand)));
    }
  }

  @Test
  public void testControlNodeCommandWithInvalidParam() {
    for (TestData t : testData) {
      NodeTaskParams params = new NodeTaskParams();
      params.cloud = t.cloudType;
      params.azUuid = t.zone.uuid;
      params.nodeName = t.node.getNodeName();

      try {
        devOpsHelper.nodeCommand(DevOpsHelper.NodeCommandType.Control, params);
      } catch (RuntimeException re) {
        assertThat(re.getMessage(), is("NodeTaskParams is not AnsibleClusterServerCtl.Params"));
      }
    }
  }

  @Test
  public void testControlNodeCommand() {
    for (TestData t : testData) {
      AnsibleClusterServerCtl.Params params = new AnsibleClusterServerCtl.Params();
      params.cloud = t.cloudType;
      params.azUuid = t.zone.uuid;
      params.nodeName = t.node.getNodeName();
      params.process = "master";
      params.command = "create";

      String command = devOpsHelper.nodeCommand(DevOpsHelper.NodeCommandType.Control, params);
      String expectedCommand = t.baseCommand + " instance control " +
        params.process + " " + params.command + " " +  params.nodeName;
      assertThat(command, allOf(notNullValue(), equalTo(expectedCommand)));
    }
  }

  @Test
  public void testDockerNodeCommandWithoutDockerNetwork() {
    for (TestData t : testData) {
      AnsibleUpdateNodeInfo.Params params = new AnsibleUpdateNodeInfo.Params();
      params.cloud = t.cloudType;
      params.azUuid = t.zone.uuid;
      params.nodeName = t.node.getNodeName();
      try {
        devOpsHelper.nodeCommand(DevOpsHelper.NodeCommandType.List, params);
      } catch (RuntimeException re) {
        if (t.cloudType == Common.CloudType.docker) {
          assertThat(
              re.getMessage(), allOf(notNullValue(), is("yb.docker.network is not set in application.conf")));
        }
      }
    }
  }

  @Test
  public void testDockerNodeCommandWithDockerNetwork() {
    when(mockAppConfig.getString("yb.docker.network")).thenReturn(DOCKER_NETWORK);

    for (TestData t : testData) {
      AnsibleUpdateNodeInfo.Params params = new AnsibleUpdateNodeInfo.Params();
      params.cloud = t.cloudType;
      params.azUuid = t.zone.uuid;
      params.nodeName = t.node.getNodeName();
      String command = devOpsHelper.nodeCommand(DevOpsHelper.NodeCommandType.List, params);
      System.out.println(command);
      String expectedCommand = t.baseCommand + " instance list --as_json " + params.nodeName;
      assertThat(command, allOf(notNullValue(), equalTo(expectedCommand)));
    }
  }
}
