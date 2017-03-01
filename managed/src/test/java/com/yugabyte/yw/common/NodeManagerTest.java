// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.common;

import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase;
import com.yugabyte.yw.commissioner.tasks.UpgradeUniverse;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleConfigureServers;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleDestroyServer;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleSetupServer;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleUpdateNodeInfo;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleClusterServerCtl;
import com.yugabyte.yw.forms.NodeInstanceFormData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
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
import java.util.LinkedList;
import java.util.List;
import java.util.UUID;

import static com.yugabyte.yw.commissioner.tasks.UpgradeUniverse.UpgradeTaskSubType.Download;
import static com.yugabyte.yw.commissioner.tasks.UpgradeUniverse.UpgradeTaskSubType.Install;
import static com.yugabyte.yw.commissioner.tasks.UpgradeUniverse.UpgradeTaskType.GFlags;
import static com.yugabyte.yw.commissioner.tasks.UpgradeUniverse.UpgradeTaskType.Software;
import static org.hamcrest.CoreMatchers.*;
import static org.junit.Assert.assertThat;
import static org.junit.Assert.fail;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;


@RunWith(MockitoJUnitRunner.class)
public class NodeManagerTest extends FakeDBApplication {

  @Mock
  play.Configuration mockAppConfig;

  @Mock
  ShellProcessHandler shellProcessHandler;

  @InjectMocks
  NodeManager nodeManager;

  private final String DOCKER_NETWORK = "yugaware_bridge";
  private final String MASTER_ADDRESSES = "host-n1:7100,host-n2:7100,host-n3:7100";
  private final String fakeMountPath1 = "/fake/path/d0";
  private final String fakeMountPath2 = "/fake/path/d1";

  private class TestData {
    public Common.CloudType cloudType;
    public Provider provider;
    public Region region;
    public AvailabilityZone zone;
    public NodeInstance node;
    public List<String> baseCommand = new ArrayList<>();

    public final String instanceTypeCode = "fake_instance_type";

    public TestData(Customer customer, Common.CloudType cloud) {
      cloudType = cloud;
      provider = ModelFactory.newProvider(customer, cloud);
      region = Region.create(provider, "region-1", "Region 1", "yb-image-1");
      zone = AvailabilityZone.create(region, "az-1", "AZ 1", "subnet-1");

      NodeInstanceFormData formData = new NodeInstanceFormData();
      formData.ip = "fake_ip";
      formData.region = region.code;
      formData.zone = zone.code;
      formData.instanceType = instanceTypeCode;
      node = NodeInstance.create(zone.uuid, formData);
      // Update name.
      node.setNodeName("fake_name:" + provider.code);
      node.save();

      // Add custom volumes mount paths
      InstanceType.VolumeDetails volume1 = new InstanceType.VolumeDetails();
      volume1.volumeType = InstanceType.VolumeType.SSD;
      volume1.volumeSizeGB = 100;
      volume1.mountPath = fakeMountPath1;
      InstanceType.VolumeDetails volume2 = new InstanceType.VolumeDetails();
      volume2.volumeType = InstanceType.VolumeType.SSD;
      volume2.volumeSizeGB = 100;
      volume2.mountPath = fakeMountPath2;
      InstanceType.InstanceTypeDetails instanceTypeDetails = new InstanceType.InstanceTypeDetails();
      instanceTypeDetails.volumeDetailsList.add(volume1);
      instanceTypeDetails.volumeDetailsList.add(volume2);
      InstanceType.upsert(provider.code,
                          instanceTypeCode,
                          0,
                          0.0,
                          0,
                          0,
                          InstanceType.VolumeType.SSD,
                          instanceTypeDetails);

      baseCommand.add("bin/ybcloud.sh");
      baseCommand.add(provider.code);
      baseCommand.add("--zone");
      baseCommand.add(zone.code);
      baseCommand.add("--region");
      baseCommand.add(region.code);

      if (cloudType == Common.CloudType.docker) {
        baseCommand.add("--network");
        baseCommand.add(DOCKER_NETWORK);
      }

      if (cloudType == Common.CloudType.onprem) {
        baseCommand.add("--node_metadata");
        baseCommand.add(node.getDetailsJson());
      }
    }
  }

  private List<TestData> testData;

  private Universe createUniverse() {
    UUID uuid = UUID.randomUUID();
    return Universe.create("Test universe " + uuid.toString(), uuid, 1L);
  }

  private void buildValidParams(TestData testData, NodeTaskParams params, Universe universe) {
    params.cloud = testData.cloudType;
    params.azUuid = testData.zone.uuid;
    params.instanceType = testData.node.instanceTypeCode;
    params.nodeName = testData.node.getNodeName();
    params.universeUUID = universe.universeUUID;
  }

  private NodeTaskParams createInvalidParams(TestData testData) {
    Universe u = createUniverse();
    NodeTaskParams params = new NodeTaskParams();
    params.cloud = testData.cloudType;
    params.azUuid = testData.zone.uuid;
    params.nodeName = testData.node.getNodeName();
    params.universeUUID = u.universeUUID;
    return params;
  }

  @Before
  public void setUp() {
    Customer customer = ModelFactory.testCustomer();
    testData = new ArrayList<TestData>();
    testData.add(new TestData(customer, Common.CloudType.aws));
    testData.add(new TestData(customer, Common.CloudType.onprem));
    when(mockAppConfig.getString("yb.devops.home")).thenReturn("/my/devops");
  }

  private List<String> nodeCommand(NodeManager.NodeCommandType type, NodeTaskParams params) {
    List<String> expectedCommand = new ArrayList<>();

    expectedCommand.add("instance");
    expectedCommand.add(type.toString().toLowerCase());
    switch(type) {
      case List:
        expectedCommand.add("--as_json");
        break;
      case Control:
        AnsibleClusterServerCtl.Params ctlParams = (AnsibleClusterServerCtl.Params) params;
        expectedCommand.add(ctlParams.process);
        expectedCommand.add(ctlParams.command);
        break;
      case Provision:
        AnsibleSetupServer.Params setupParams = (AnsibleSetupServer.Params) params;
        if (params.cloud != Common.CloudType.onprem) {
          expectedCommand.add("--instance_type");
          expectedCommand.add(setupParams.instanceType);
          expectedCommand.add("--cloud_subnet");
          expectedCommand.add(setupParams.subnetId );
          expectedCommand.add("--machine_image");
          expectedCommand.add(setupParams.getRegion().ybImage);
          expectedCommand.add("--assign_public_ip");
        }
        break;
      case Configure:
        AnsibleConfigureServers.Params configureParams = (AnsibleConfigureServers.Params) params;

        expectedCommand.add("--master_addresses_for_tserver");
        expectedCommand.add(MASTER_ADDRESSES);
        if (!configureParams.isMasterInShellMode) {
          expectedCommand.add("--master_addresses_for_master");
          expectedCommand.add(MASTER_ADDRESSES);
        }
        if (configureParams.ybServerPackage != null) {
          expectedCommand.add("--package");
          expectedCommand.add(configureParams.ybServerPackage);
        }

        if (configureParams.getProperty("taskSubType") != null) {
          UpgradeUniverse.UpgradeTaskSubType taskSubType =
              UpgradeUniverse.UpgradeTaskSubType.valueOf(configureParams.getProperty("taskSubType"));
          switch(taskSubType) {
            case Download:
              expectedCommand.add("--tags");
              expectedCommand.add("download-software");
              break;
            case Install:
              expectedCommand.add("--tags");
              expectedCommand.add("install-software");
              break;
          }
        }

        if (!configureParams.gflags.isEmpty()) {
          String gflagsJson =  Json.stringify(Json.toJson(configureParams.gflags));
          expectedCommand.add("--replace_gflags");
          expectedCommand.add("--gflags");
          expectedCommand.add(gflagsJson);
        }
        break;
    }
    if (!(params.instanceType == null || params.instanceType.isEmpty())) {
      expectedCommand.add("--mount_points");
      expectedCommand.add(fakeMountPath1 + "," + fakeMountPath2);
    }

    expectedCommand.add(params.nodeName);
    return expectedCommand;
  }

  @Test
  public void testAddMountPathsInvalidParamsFail() {
    try {
      AnsibleSetupServer.Params params = new AnsibleSetupServer.Params();
      buildValidParams(testData.get(0), params, createUniverse());
      params.instanceType = "fakeTypeBlah";

      nodeManager.nodeCommand(NodeManager.NodeCommandType.Provision, params);
      fail();
    } catch (RuntimeException re) {
      String errMsg = "No InstanceType exists for provider code fake1 and instance type code fake2";
      assertThat(re.getMessage(), is(errMsg));
    }
  }

  @Test
  public void testProvisionNodeCommand() {
    for (TestData t : testData) {
      AnsibleSetupServer.Params params = new AnsibleSetupServer.Params();
      buildValidParams(t, params, createUniverse());
      params.subnetId = t.zone.subnet;

      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Provision, params));

      nodeManager.nodeCommand(NodeManager.NodeCommandType.Provision, params);
      verify(shellProcessHandler, times(1)).run(expectedCommand, t.region.provider.getConfig());
    }
  }

  @Test
  public void testProvisionNodeCommandWithInvalidParam() {
    for (TestData t : testData) {
      try {
        nodeManager.nodeCommand(NodeManager.NodeCommandType.Provision, createInvalidParams(t));
      } catch (RuntimeException re) {
        assertThat(re.getMessage(), is("NodeTaskParams is not AnsibleSetupServer.Params"));
      }
    }
  }

  @Test
  public void testConfigureNodeCommandWithInvalidParam() {
    for (TestData t : testData) {
      try {
        nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, createInvalidParams(t));
      } catch (RuntimeException re) {
        assertThat(re.getMessage(), is("NodeTaskParams is not AnsibleConfigureServers.Params"));
      }
    }
  }

  @Test
  public void testConfigureNodeCommand() {
    for (TestData t : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(t, params, Universe.saveDetails(createUniverse().universeUUID,
          ApiUtils.mockUniverseUpdater()));
      params.isMasterInShellMode = true;
      params.ybServerPackage = "yb-server-pkg";
      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Configure, params));

      nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
      verify(shellProcessHandler, times(1)).run(expectedCommand, t.region.provider.getConfig());
    }
  }

  @Test
  public void testConfigureNodeCommandWithAccessKey() {
    for (TestData t : testData) {
      // Create AccessKey
      AccessKey.KeyInfo keyInfo = new AccessKey.KeyInfo();
      keyInfo.privateKey = "/path/to/private.key";
      keyInfo.publicKey = "/path/to/public.key";
      keyInfo.vaultFile = "/path/to/vault_file";
      keyInfo.vaultPasswordFile = "/path/to/vault_password";
      AccessKey.create(t.provider.uuid, "demo-access", keyInfo);

      // Set up task params
      UniverseDefinitionTaskParams.UserIntent userIntent =
          new UniverseDefinitionTaskParams.UserIntent();
      userIntent.numNodes = 3;
      userIntent.accessKeyCode = "demo-access";
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(t, params, Universe.saveDetails(createUniverse().universeUUID,
          ApiUtils.mockUniverseUpdater(userIntent)));
      params.isMasterInShellMode = true;
      params.ybServerPackage = "yb-server-pkg";

      // Set up expected command
      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Configure, params));
      List<String> accessKeyCommand = ImmutableList.of("--vars_file", "/path/to/vault_file",
          "--vault_password_file", "/path/to/vault_password", "--private_key_file",
          "/path/to/private.key");
      expectedCommand.addAll(expectedCommand.size() - 3, accessKeyCommand);

      nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
      verify(shellProcessHandler, times(1)).run(expectedCommand, t.region.provider.getConfig());
    }
  }

  @Test
  public void testConfigureNodeCommandInShellMode() {
    for (TestData t : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(t, params, Universe.saveDetails(createUniverse().universeUUID,
          ApiUtils.mockUniverseUpdater()));
      params.isMasterInShellMode = false;
      params.ybServerPackage = "yb-server-pkg";

      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Configure, params));
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
      verify(shellProcessHandler, times(1)).run(expectedCommand, t.region.provider.getConfig());
    }
  }

  @Test
  public void testSoftwareUpgradeWithoutRequiredProperties() {
    for (TestData t : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(t, params, Universe.saveDetails(createUniverse().universeUUID,
          ApiUtils.mockUniverseUpdater()));
      params.type = Software;
      params.ybServerPackage = "yb-server-pkg";

      try {
        nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);

      } catch (RuntimeException re) {
        assertThat(re.getMessage(), allOf(notNullValue(), is("Invalid taskSubType property: null")));
      }
    }
  }

  @Test
  public void testSoftwareUpgradeWithDownloadNodeCommand() {
    for (TestData t : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(t, params, Universe.saveDetails(createUniverse().universeUUID,
          ApiUtils.mockUniverseUpdater()));
      params.type = Software;
      params.ybServerPackage = "yb-server-pkg";
      params.isMasterInShellMode = true;
      params.setProperty("taskSubType", Download.toString());

      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Configure, params));
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
      verify(shellProcessHandler, times(1)).run(expectedCommand, t.region.provider.getConfig());
    }
  }

  @Test
  public void testSoftwareUpgradeWithInstallNodeCommand() {
    for (TestData t : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(t, params, Universe.saveDetails(createUniverse().universeUUID,
          ApiUtils.mockUniverseUpdater()));
      params.type = Software;
      params.ybServerPackage = "yb-server-pkg";
      params.isMasterInShellMode = true;
      params.setProperty("taskSubType", Install.toString());

      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Configure, params));
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
      verify(shellProcessHandler, times(1)).run(expectedCommand, t.region.provider.getConfig());
    }
  }

  @Test
  public void testGFlagsUpgradeWithoutRequiredProperties() {
    for (TestData t : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(t, params, Universe.saveDetails(createUniverse().universeUUID,
          ApiUtils.mockUniverseUpdater()));
      params.nodeName = t.node.getNodeName();
      HashMap<String, String> gflags = new HashMap<>();
      gflags.put("gflagName", "gflagValue");
      params.gflags = gflags;
      params.type = GFlags;
      params.isMasterInShellMode = true;

      try {
        nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);

      } catch (RuntimeException re) {
        assertThat(re.getMessage(), allOf(notNullValue(), is("Invalid processType property: null")));
      }
    }
  }

  @Test
  public void testGFlagsUpgradeWithEmptyGFlagsNodeCommand() {
    for (TestData t : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(t, params, Universe.saveDetails(createUniverse().universeUUID,
          ApiUtils.mockUniverseUpdater()));
      params.nodeName = t.node.getNodeName();
      params.type = GFlags;

      try {
        nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);

      } catch (RuntimeException re) {
        assertThat(re.getMessage(), allOf(notNullValue(), is("Empty GFlags data provided")));
      }
    }
  }

  @Test
  public void testGFlagsUpgradeForMasterNodeCommand() {
    for (TestData t : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(t, params, Universe.saveDetails(createUniverse().universeUUID,
          ApiUtils.mockUniverseUpdater()));
      params.nodeName = t.node.getNodeName();
      HashMap<String, String> gflags = new HashMap<>();
      gflags.put("gflagName", "gflagValue");
      params.gflags = gflags;
      params.type = GFlags;
      params.isMasterInShellMode = true;
      params.setProperty("processType", UniverseDefinitionTaskBase.ServerType.MASTER.toString());

      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Configure, params));
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
      verify(shellProcessHandler, times(1)).run(expectedCommand, t.region.provider.getConfig());
    }
  }

  @Test
  public void testDestroyNodeCommandWithInvalidParam() {
    for (TestData t : testData) {
      try {
        nodeManager.nodeCommand(NodeManager.NodeCommandType.Destroy, createInvalidParams(t));
      } catch (RuntimeException re) {
        assertThat(re.getMessage(), is("NodeTaskParams is not AnsibleDestroyServer.Params"));
      }
    }
  }

  @Test
  public void testDestroyNodeCommand() {
    for (TestData t : testData) {
      AnsibleDestroyServer.Params params = new AnsibleDestroyServer.Params();
      buildValidParams(t, params, createUniverse());

      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Destroy, params));
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Destroy, params);
      verify(shellProcessHandler, times(1)).run(expectedCommand, t.region.provider.getConfig());
    }
  }

  @Test
  public void testListNodeCommandWithInvalidParam() {
    for (TestData t : testData) {
      try {
        nodeManager.nodeCommand(NodeManager.NodeCommandType.List, createInvalidParams(t));
      } catch (RuntimeException re) {
        assertThat(re.getMessage(), is("NodeTaskParams is not AnsibleUpdateNodeInfo.Params"));
      }
    }
  }

  @Test
  public void testListNodeCommand() {
    for (TestData t : testData) {
      AnsibleUpdateNodeInfo.Params params = new AnsibleUpdateNodeInfo.Params();
      buildValidParams(t, params, createUniverse());

      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.List, params));
      nodeManager.nodeCommand(NodeManager.NodeCommandType.List, params);
      verify(shellProcessHandler, times(1)).run(expectedCommand, t.region.provider.getConfig());
    }
  }

  @Test
  public void testControlNodeCommandWithInvalidParam() {
    for (TestData t : testData) {
      try {
        nodeManager.nodeCommand(NodeManager.NodeCommandType.Control, createInvalidParams(t));
      } catch (RuntimeException re) {
        assertThat(re.getMessage(), is("NodeTaskParams is not AnsibleClusterServerCtl.Params"));
      }
    }
  }

  @Test
  public void testControlNodeCommand() {
    for (TestData t : testData) {
      AnsibleClusterServerCtl.Params params = new AnsibleClusterServerCtl.Params();
      buildValidParams(t, params, createUniverse());
      params.process = "master";
      params.command = "create";

      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Control, params));
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Control, params);
      verify(shellProcessHandler, times(1)).run(expectedCommand, t.region.provider.getConfig());
    }
  }

  @Test
  public void testDockerNodeCommandWithoutDockerNetwork() {
    for (TestData t : testData) {
      AnsibleUpdateNodeInfo.Params params = new AnsibleUpdateNodeInfo.Params();
      buildValidParams(t, params, createUniverse());

      try {
        nodeManager.nodeCommand(NodeManager.NodeCommandType.List, params);
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
      buildValidParams(t, params, createUniverse());

      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.List, params));
      nodeManager.nodeCommand(NodeManager.NodeCommandType.List, params);
      verify(shellProcessHandler, times(1)).run(expectedCommand, t.region.provider.getConfig());
    }
  }
}
