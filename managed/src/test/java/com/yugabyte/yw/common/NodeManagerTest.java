// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.common;

import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.cloud.PublicCloudConstants;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase;
import com.yugabyte.yw.commissioner.tasks.UpgradeUniverse;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleConfigureServers;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleDestroyServer;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleSetupServer;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleClusterServerCtl;
import com.yugabyte.yw.forms.NodeInstanceFormData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.*;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.runners.MockitoJUnitRunner;
import play.libs.Json;

import java.io.File;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.function.Predicate;

import static com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase.ServerType.MASTER;
import static com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase.ServerType.TSERVER;
import static com.yugabyte.yw.commissioner.tasks.UpgradeUniverse.UpgradeTaskSubType.Download;
import static com.yugabyte.yw.commissioner.tasks.UpgradeUniverse.UpgradeTaskSubType.Install;
import static com.yugabyte.yw.commissioner.tasks.UpgradeUniverse.UpgradeTaskType.Everything;
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

  @Mock
  ReleaseManager releaseManager;

  @InjectMocks
  NodeManager nodeManager;

  private final String DOCKER_NETWORK = "yugaware_bridge";
  private final String MASTER_ADDRESSES = "host-n1:7100,host-n2:7100,host-n3:7100";
  private final String fakeMountPath1 = "/fake/path/d0";
  private final String fakeMountPath2 = "/fake/path/d1";
  private final String fakeMountPaths = fakeMountPath1 + "," + fakeMountPath2;
  private final String instanceTypeCode = "fake_instance_type";

  private class TestData {
    public Common.CloudType cloudType;
    public PublicCloudConstants.EBSType ebsType;
    public Provider provider;
    public Region region;
    public AvailabilityZone zone;
    public NodeInstance node;
    public List<String> baseCommand = new ArrayList<>();

    public TestData(Provider p, Common.CloudType cloud, PublicCloudConstants.EBSType ebs) {
      cloudType = cloud;
      ebsType = ebs;
      provider = p;
      region = Region.create(provider, "region-1", "Region 1", "yb-image-1");
      zone = AvailabilityZone.create(region, "az-1", "AZ 1", "subnet-1");

      NodeInstanceFormData.NodeInstanceData nodeData = new NodeInstanceFormData.NodeInstanceData();
      nodeData.ip = "fake_ip";
      nodeData.region = region.code;
      nodeData.zone = zone.code;
      nodeData.instanceType = instanceTypeCode;
      node = NodeInstance.create(zone.uuid, nodeData);
      // Update name.
      node.setNodeName("fake_name:" + provider.code);
      node.save();

      baseCommand.add("bin/ybcloud.sh");
      baseCommand.add(provider.code);
      baseCommand.add("--region");
      baseCommand.add(region.code);
      baseCommand.add("--zone");
      baseCommand.add(zone.code);

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

  public List<TestData> getTestData(Customer customer, Common.CloudType cloud) {
    List<TestData> testDataList = new ArrayList<>();
    Provider provider = ModelFactory.newProvider(customer, cloud);
    if (cloud.equals(Common.CloudType.aws)) {
      testDataList.add(new TestData(provider, cloud, PublicCloudConstants.EBSType.GP2));
      testDataList.add(new TestData(provider, cloud, PublicCloudConstants.EBSType.IO1));
    } else {
      testDataList.add(new TestData(provider, cloud, null));
    }
    return testDataList;
  }

  private Universe createUniverse() {
    UUID universeUUID = UUID.randomUUID();
    return ModelFactory.createUniverse("Test Universe - " + universeUUID, universeUUID);
  }

  private void buildValidParams(TestData testData, NodeTaskParams params, Universe universe) {
    params.azUuid = testData.zone.uuid;
    params.instanceType = testData.node.instanceTypeCode;
    params.nodeName = testData.node.getNodeName();
    params.universeUUID = universe.universeUUID;
  }

  private void addValidDeviceInfo(TestData testData, NodeTaskParams params) {
    params.deviceInfo = new DeviceInfo();
    params.deviceInfo.mountPoints = fakeMountPaths;
    params.deviceInfo.volumeSize = 200;
    params.deviceInfo.numVolumes = 2;
    if (testData.cloudType.equals(Common.CloudType.aws)) {
      params.deviceInfo.ebsType = testData.ebsType;
      if (testData.ebsType != null && testData.ebsType.equals(PublicCloudConstants.EBSType.IO1)) {
        params.deviceInfo.diskIops = 240;
      }
    }
  }

  private NodeTaskParams createInvalidParams(TestData testData) {
    Universe u = createUniverse();
    NodeTaskParams params = new NodeTaskParams();
    params.azUuid = testData.zone.uuid;
    params.nodeName = testData.node.getNodeName();
    params.universeUUID = u.universeUUID;
    return params;
  }

  private AccessKey getOrCreate(UUID providerUUID, String keyCode, AccessKey.KeyInfo keyInfo) {
    AccessKey accessKey = AccessKey.get(providerUUID, keyCode);
    if (accessKey == null) {
      accessKey = AccessKey.create(providerUUID, keyCode, keyInfo);
    }
    return accessKey;
  }

  @Before
  public void setUp() {
    Customer customer = ModelFactory.testCustomer();
    testData = new ArrayList<TestData>();
    testData.addAll(getTestData(customer, Common.CloudType.aws));
    testData.addAll(getTestData(customer, Common.CloudType.gcp));
    testData.addAll(getTestData(customer, Common.CloudType.onprem));
    when(mockAppConfig.getString("yb.devops.home")).thenReturn("/my/devops");
    when(releaseManager.getReleaseByVersion("0.0.1")).thenReturn("/yb/release.tar.gz");
  }

  private List<String> nodeCommand(NodeManager.NodeCommandType type, NodeTaskParams params,
                                   Common.CloudType cloud) {
    List<String> expectedCommand = new ArrayList<>();

    expectedCommand.add("instance");
    expectedCommand.add(type.toString().toLowerCase());
    switch (type) {
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
        if (!cloud.equals(Common.CloudType.onprem)) {
          expectedCommand.add("--instance_type");
          expectedCommand.add(setupParams.instanceType);
          expectedCommand.add("--cloud_subnet");
          expectedCommand.add(setupParams.subnetId);
          if (setupParams.spotPrice > 0.0) {
            if (cloud.equals(Common.CloudType.aws)) {
              expectedCommand.add("--spot_price");
              expectedCommand.add(Double.toString(setupParams.spotPrice));
            } else if (cloud.equals(Common.CloudType.aws)) {
              expectedCommand.add("--use_preemptible");
            }
          }
          if (!cloud.equals(Common.CloudType.aws) && !cloud.equals(Common.CloudType.gcp)) {
            expectedCommand.add("--machine_image");
            expectedCommand.add(setupParams.getRegion().ybImage);
          }
          if (setupParams.assignPublicIP) {
            expectedCommand.add("--assign_public_ip");
          }
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
        if (configureParams.ybSoftwareVersion != null) {
          expectedCommand.add("--package");
          expectedCommand.add("/yb/release.tar.gz");
        }

        if (configureParams.getProperty("taskSubType") != null) {
          UpgradeUniverse.UpgradeTaskSubType taskSubType =
              UpgradeUniverse.UpgradeTaskSubType.valueOf(configureParams.getProperty("taskSubType"));
          String processType = configureParams.getProperty("processType");
          expectedCommand.add("--yb_process_type");
          expectedCommand.add(processType.toLowerCase());
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

        Map<String, String> gflags = new HashMap<>(configureParams.gflags);
        gflags.put("metric_node_name", params.nodeName);

        if (configureParams.type == Everything) {
          expectedCommand.add("--extra_gflags");
          expectedCommand.add(Json.stringify(Json.toJson(gflags)));
        } else if (configureParams.type == GFlags) {
          if (!configureParams.gflags.isEmpty()) {
            String processType = configureParams.getProperty("processType");
            expectedCommand.add("--yb_process_type");
            expectedCommand.add(processType.toLowerCase());
            String gflagsJson =  Json.stringify(Json.toJson(gflags));
            expectedCommand.add("--replace_gflags");
            expectedCommand.add("--gflags");
            expectedCommand.add(gflagsJson);
          }
        }
        break;
      case Destroy:
        expectedCommand.add("--instance_type");
        expectedCommand.add(instanceTypeCode);
        break;
    }
    if (params.deviceInfo != null) {
      DeviceInfo deviceInfo = params.deviceInfo;
      if (deviceInfo.numVolumes != null && !cloud.equals(Common.CloudType.onprem)) {
        expectedCommand.add("--num_volumes");
        expectedCommand.add(Integer.toString(deviceInfo.numVolumes));
      } else if (deviceInfo.mountPoints != null) {
        expectedCommand.add("--mount_points");
        expectedCommand.add(fakeMountPaths);
      }
      if (deviceInfo.volumeSize != null) {
        expectedCommand.add("--volume_size");
        expectedCommand.add(Integer.toString(deviceInfo.volumeSize));
      }
      if (type == NodeManager.NodeCommandType.Provision && deviceInfo.ebsType != null) {
        expectedCommand.add("--volume_type");
        expectedCommand.add(deviceInfo.ebsType.toString().toLowerCase());
        if (deviceInfo.ebsType == PublicCloudConstants.EBSType.IO1 && deviceInfo.diskIops != null) {
          expectedCommand.add("--disk_iops");
          expectedCommand.add(Integer.toString(deviceInfo.diskIops));
        }
      }

      String packagePath = mockAppConfig.getString("yb.thirdparty.packagePath");
      if (type == NodeManager.NodeCommandType.Provision && packagePath != null) {
        expectedCommand.add("--local_package_path");
        expectedCommand.add(packagePath);
      }
    }

    expectedCommand.add(params.nodeName);
    return expectedCommand;
  }

  @Test
  public void testProvisionNodeCommand() {
    for (TestData t : testData) {
      AnsibleSetupServer.Params params = new AnsibleSetupServer.Params();
      buildValidParams(t, params, Universe.saveDetails(createUniverse().universeUUID,
          ApiUtils.mockUniverseUpdater(t.cloudType)));
      addValidDeviceInfo(t, params);
      params.subnetId = t.zone.subnet;

      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Provision, params, t.cloudType));

      nodeManager.nodeCommand(NodeManager.NodeCommandType.Provision, params);
      verify(shellProcessHandler, times(1)).run(expectedCommand, t.region.provider.getConfig());
    }
  }

  @Test
  public void testProvisionNodeCommandWithSpotPrice() {
    for (TestData t : testData) {
      AnsibleSetupServer.Params params = new AnsibleSetupServer.Params();
      buildValidParams(t, params, Universe.saveDetails(createUniverse().universeUUID,
          ApiUtils.mockUniverseUpdater(t.cloudType)));
      addValidDeviceInfo(t, params);
      params.subnetId = t.zone.subnet;
      if (t.cloudType.equals(Common.CloudType.aws)) {
        params.spotPrice = 0.2;
      }

      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Provision, params, t.cloudType));

      nodeManager.nodeCommand(NodeManager.NodeCommandType.Provision, params);
      verify(shellProcessHandler, times(1)).run(expectedCommand, t.region.provider.getConfig());
    }
  }

  @Test
  public void testProvisionNodeCommandWithoutAssignPublicIP() {
    for (TestData t : testData) {
      AnsibleSetupServer.Params params = new AnsibleSetupServer.Params();
      buildValidParams(t, params, Universe.saveDetails(createUniverse().universeUUID,
              ApiUtils.mockUniverseUpdater(t.cloudType)));
      addValidDeviceInfo(t, params);
      params.subnetId = t.zone.subnet;
      if (t.cloudType.equals(Common.CloudType.aws)) {
        params.assignPublicIP = false;
      }

      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Provision, params, t.cloudType));
      if (t.cloudType.equals(Common.CloudType.aws)) {
        Predicate<String> stringPredicate = p -> p.equals("--assign_public_ip");
        expectedCommand.removeIf(stringPredicate);
      }
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Provision, params);
      verify(shellProcessHandler, times(1)).run(expectedCommand, t.region.provider.getConfig());
    }
  }

  @Test
  public void testProvisionNodeCommandWithLocalPackage() {
    String packagePath = "/tmp/third-party";
    new File(packagePath).mkdir();
    when(mockAppConfig.getString("yb.thirdparty.packagePath")).thenReturn(packagePath);

    for (TestData t : testData) {
      AnsibleSetupServer.Params params = new AnsibleSetupServer.Params();
      buildValidParams(t, params, Universe.saveDetails(createUniverse().universeUUID,
          ApiUtils.mockUniverseUpdater(t.cloudType)));
      addValidDeviceInfo(t, params);
      params.subnetId = t.zone.subnet;

      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Provision, params, t.cloudType));

      nodeManager.nodeCommand(NodeManager.NodeCommandType.Provision, params);
      verify(shellProcessHandler, times(1)).run(expectedCommand, t.region.provider.getConfig());
    }

    File file = new File(packagePath);
    file.delete();
  }

  @Test
  public void testProvisionNodeCommandWithAccessKey() {
    for (TestData t : testData) {
      // Create AccessKey
      AccessKey.KeyInfo keyInfo = new AccessKey.KeyInfo();
      keyInfo.privateKey = "/path/to/private.key";
      keyInfo.publicKey = "/path/to/public.key";
      keyInfo.vaultFile = "/path/to/vault_file";
      keyInfo.vaultPasswordFile = "/path/to/vault_password";
      keyInfo.airGapInstall = true;
      getOrCreate(t.provider.uuid, "demo-access", keyInfo);

      // Set up task params
      UniverseDefinitionTaskParams.UserIntent userIntent =
          new UniverseDefinitionTaskParams.UserIntent();
      userIntent.numNodes = 3;
      userIntent.accessKeyCode = "demo-access";
      userIntent.regionList = new ArrayList<UUID>();
      userIntent.regionList.add(t.region.uuid);
      userIntent.providerType = t.cloudType;
      AnsibleSetupServer.Params params = new AnsibleSetupServer.Params();
      buildValidParams(t, params, Universe.saveDetails(createUniverse().universeUUID,
          ApiUtils.mockUniverseUpdater(userIntent)));
      addValidDeviceInfo(t, params);

      // Set up expected command
      int accessKeyIndexOffset = 5;
      if (t.cloudType.equals(Common.CloudType.aws)) {
        accessKeyIndexOffset += 2;
        if (params.deviceInfo.ebsType.equals(PublicCloudConstants.EBSType.IO1)) {
          accessKeyIndexOffset += 2;
        }
      }
      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Provision, params, t.cloudType));
      List<String> accessKeyCommand = new ArrayList<String>(ImmutableList.of("--vars_file", "/path/to/vault_file",
          "--vault_password_file", "/path/to/vault_password", "--private_key_file",
          "/path/to/private.key"));
      if (t.cloudType.equals(Common.CloudType.onprem)) {
        accessKeyCommand.add("--air_gap");
      }
      expectedCommand.addAll(expectedCommand.size() - accessKeyIndexOffset, accessKeyCommand);
      if (t.cloudType.equals(Common.CloudType.aws)) {
        List<String> awsAccessKeyCommands = ImmutableList.of("--key_pair_name",
            userIntent.accessKeyCode, "--security_group", "yb-" +  t.region.code + "-sg");
        expectedCommand.addAll(expectedCommand.size() - accessKeyIndexOffset, awsAccessKeyCommands);
      }

      nodeManager.nodeCommand(NodeManager.NodeCommandType.Provision, params);
      verify(shellProcessHandler, times(1)).run(expectedCommand, t.region.provider.getConfig());
    }
  }

  @Test
  public void testProvisionNodeCommandWithInvalidParam() {
    for (TestData t : testData) {
      try {
        nodeManager.nodeCommand(NodeManager.NodeCommandType.Provision, createInvalidParams(t));
        fail();
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
        fail();
      } catch (RuntimeException re) {
        assertThat(re.getMessage(), is("NodeTaskParams is not AnsibleConfigureServers.Params"));
      }
    }
  }

  @Test
  public void testConfigureNodeCommand() {
    Map<Common.CloudType, Integer> expectedInvocations = new HashMap<>();
    for (TestData t : testData) {
      expectedInvocations.put(t.cloudType, expectedInvocations.getOrDefault(t.cloudType, 0) + 1);
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(t, params, Universe.saveDetails(createUniverse().universeUUID,
          ApiUtils.mockUniverseUpdater(t.cloudType)));
      addValidDeviceInfo(t, params);
      params.isMasterInShellMode = true;
      params.ybSoftwareVersion = "0.0.1";
      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Configure, params, t.cloudType));

      nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
      verify(shellProcessHandler, times(expectedInvocations.get(t.cloudType))).run(expectedCommand,
          t.region.provider.getConfig());
    }
  }

  @Test
  public void testConfigureNodeCommandWithoutReleasePackage() {
    for (TestData t : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(t, params, Universe.saveDetails(createUniverse().universeUUID,
          ApiUtils.mockUniverseUpdater(t.cloudType)));
      params.isMasterInShellMode = false;
      params.ybSoftwareVersion = "0.0.2";
      try {
        nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
        fail();
      } catch (RuntimeException re) {
        assertThat(re.getMessage(), allOf(notNullValue(),
            is("Unable to fetch yugabyte release for version: 0.0.2")));
      }
    }
  }

  @Test
  public void testConfigureNodeCommandWithAccessKey() {
    Map<Common.CloudType, Integer> expectedInvocations = new HashMap<>();
    for (TestData t : testData) {
      expectedInvocations.put(t.cloudType, expectedInvocations.getOrDefault(t.cloudType, 0) + 1);
      // Create AccessKey
      AccessKey.KeyInfo keyInfo = new AccessKey.KeyInfo();
      keyInfo.privateKey = "/path/to/private.key";
      keyInfo.publicKey = "/path/to/public.key";
      keyInfo.vaultFile = "/path/to/vault_file";
      keyInfo.vaultPasswordFile = "/path/to/vault_password";
      getOrCreate(t.provider.uuid, "demo-access", keyInfo);

      // Set up task params
      UniverseDefinitionTaskParams.UserIntent userIntent =
          new UniverseDefinitionTaskParams.UserIntent();
      userIntent.numNodes = 3;
      userIntent.accessKeyCode = "demo-access";
      userIntent.regionList = new ArrayList<UUID>();
      userIntent.regionList.add(t.region.uuid);
      userIntent.providerType = t.cloudType;
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(t, params, Universe.saveDetails(createUniverse().universeUUID,
          ApiUtils.mockUniverseUpdater(userIntent, true /* setMasters */)));
      addValidDeviceInfo(t, params);
      params.isMasterInShellMode = true;
      params.ybSoftwareVersion = "0.0.1";

      // Set up expected command
      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Configure, params, t.cloudType));
      List<String> accessKeyCommand = ImmutableList.of(
          "--vars_file", "/path/to/vault_file", "--vault_password_file", "/path/to/vault_password",
          "--private_key_file", "/path/to/private.key");
      expectedCommand.addAll(expectedCommand.size() - 5, accessKeyCommand);

      nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
      verify(shellProcessHandler, times(expectedInvocations.get(t.cloudType))).run(expectedCommand,
          t.region.provider.getConfig());
    }
  }

  @Test
  public void testConfigureNodeCommandInShellMode() {
    Map<Common.CloudType, Integer> expectedInvocations = new HashMap<>();
    for (TestData t : testData) {
      expectedInvocations.put(t.cloudType, expectedInvocations.getOrDefault(t.cloudType, 0) + 1);
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(t, params, Universe.saveDetails(createUniverse().universeUUID,
          ApiUtils.mockUniverseUpdater(t.cloudType)));
      params.isMasterInShellMode = false;
      params.ybSoftwareVersion = "0.0.1";

      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Configure, params, t.cloudType));
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
      verify(shellProcessHandler, times(expectedInvocations.get(t.cloudType))).run(expectedCommand,
          t.region.provider.getConfig());
    }
  }

  @Test
  public void testSoftwareUpgradeWithoutProcessType() {
    for (TestData t : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(t, params, Universe.saveDetails(createUniverse().universeUUID,
          ApiUtils.mockUniverseUpdater(t.cloudType)));
      params.type = Software;
      params.ybSoftwareVersion = "0.0.1";

      try {
        nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
        fail();
      } catch (RuntimeException re) {
        assertThat(re.getMessage(), allOf(notNullValue(), is("Invalid processType: null")));
      }
    }
  }

  @Test
  public void testSoftwareUpgradeWithInvalidProcessType() {
    for (TestData t : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(t, params, Universe.saveDetails(createUniverse().universeUUID,
          ApiUtils.mockUniverseUpdater(t.cloudType)));
      params.type = Software;
      params.ybSoftwareVersion = "0.0.1";

      for (UniverseDefinitionTaskBase.ServerType type : UniverseDefinitionTaskBase.ServerType.values()) {
        try {
            // master and tserver are valid process types.
            if (ImmutableList.of(MASTER, TSERVER).contains(type)) {
              continue;
            }
            params.setProperty("processType", type.toString());
            nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
            fail();
        } catch (RuntimeException re) {
          assertThat(re.getMessage(), allOf(notNullValue(), is("Invalid processType: " + type.name())));
        }
      }
    }
  }

  @Test
  public void testSoftwareUpgradeWithoutTaskType() {
    for (TestData t : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(t, params, Universe.saveDetails(createUniverse().universeUUID,
          ApiUtils.mockUniverseUpdater(t.cloudType)));
      params.type = Software;
      params.ybSoftwareVersion = "0.0.1";
      params.setProperty("processType", MASTER.toString());

      try {
        nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
        fail();
      } catch (RuntimeException re) {
        assertThat(re.getMessage(), allOf(notNullValue(), is("Invalid taskSubType property: null")));
      }
    }
  }


  @Test
  public void testSoftwareUpgradeWithDownloadNodeCommand() {
    Map<Common.CloudType, Integer> expectedInvocations = new HashMap<>();
    for (TestData t : testData) {
      expectedInvocations.put(t.cloudType, expectedInvocations.getOrDefault(t.cloudType, 0) + 1);
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(t, params, Universe.saveDetails(createUniverse().universeUUID,
          ApiUtils.mockUniverseUpdater(t.cloudType)));
      params.type = Software;
      params.ybSoftwareVersion = "0.0.1";
      params.isMasterInShellMode = true;
      params.setProperty("taskSubType", Download.toString());
      params.setProperty("processType", MASTER.toString());
      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(
          nodeCommand(NodeManager.NodeCommandType.Configure, params, t.cloudType));
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
      verify(shellProcessHandler, times(expectedInvocations.get(t.cloudType))).run(expectedCommand,
          t.region.provider.getConfig());
    }
  }

  @Test
  public void testSoftwareUpgradeWithInstallNodeCommand() {
    Map<Common.CloudType, Integer> expectedInvocations = new HashMap<>();
    for (TestData t : testData) {
      expectedInvocations.put(t.cloudType, expectedInvocations.getOrDefault(t.cloudType, 0) + 1);
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(t, params, Universe.saveDetails(createUniverse().universeUUID,
          ApiUtils.mockUniverseUpdater(t.cloudType)));
      params.type = Software;
      params.ybSoftwareVersion = "0.0.1";
      params.isMasterInShellMode = true;
      params.setProperty("taskSubType", Install.toString());
      params.setProperty("processType", MASTER.toString());

      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Configure, params, t.cloudType));
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
      verify(shellProcessHandler, times(expectedInvocations.get(t.cloudType))).run(expectedCommand,
          t.region.provider.getConfig());
    }
  }

  @Test
  public void testSoftwareUpgradeWithoutReleasePackage() {
    for (TestData t : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(t, params, Universe.saveDetails(createUniverse().universeUUID,
          ApiUtils.mockUniverseUpdater(t.cloudType)));
      params.type = Software;
      params.ybSoftwareVersion = "0.0.2";
      params.isMasterInShellMode = true;
      params.setProperty("taskSubType", Install.toString());
      params.setProperty("processType", MASTER.toString());
      try {
        nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
        fail();
      } catch (RuntimeException re) {
        assertThat(re.getMessage(), allOf(notNullValue(),
            is("Unable to fetch yugabyte release for version: 0.0.2")));
      }
    }
  }

  @Test
  public void testGFlagsUpgradeNullProcessType() {
    for (TestData t : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(t, params, Universe.saveDetails(createUniverse().universeUUID,
          ApiUtils.mockUniverseUpdater(t.cloudType)));
      params.nodeName = t.node.getNodeName();
      HashMap<String, String> gflags = new HashMap<>();
      gflags.put("gflagName", "gflagValue");
      params.gflags = gflags;
      params.type = GFlags;
      params.isMasterInShellMode = true;

      try {
        nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
        fail();
      } catch (RuntimeException re) {
        assertThat(re.getMessage(), allOf(notNullValue(), is("Invalid processType: null")));
      }
    }
  }

  @Test
  public void testGFlagsUpgradeInvalidProcessType() {
    for (TestData t : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(t, params, Universe.saveDetails(createUniverse().universeUUID,
          ApiUtils.mockUniverseUpdater(t.cloudType)));
      params.nodeName = t.node.getNodeName();
      HashMap<String, String> gflags = new HashMap<>();
      gflags.put("gflagName", "gflagValue");
      params.gflags = gflags;
      params.type = GFlags;
      params.isMasterInShellMode = true;

      for (UniverseDefinitionTaskBase.ServerType type : UniverseDefinitionTaskBase.ServerType.values()) {
        try {
          // master and tserver are valid process types.
          if (ImmutableList.of(MASTER, TSERVER).contains(type)) {
            continue;
          }
          params.setProperty("processType", type.toString());
          nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
          fail();
        } catch (RuntimeException re) {
          assertThat(re.getMessage(), allOf(notNullValue(), is("Invalid processType: " + type.name())));
        }
      }
    }
  }

  @Test
  public void testGFlagsUpgradeWithEmptyGFlagsNodeCommand() {
    for (TestData t : testData) {
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(t, params, Universe.saveDetails(createUniverse().universeUUID,
          ApiUtils.mockUniverseUpdater(t.cloudType)));
      params.nodeName = t.node.getNodeName();
      params.type = GFlags;

      try {
        nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
        fail();
      } catch (RuntimeException re) {
        assertThat(re.getMessage(), allOf(notNullValue(), is("Empty GFlags data provided")));
      }
    }
  }

  @Test
  public void testGFlagsUpgradeForMasterNodeCommand() {
    Map<Common.CloudType, Integer> expectedInvocations = new HashMap<>();
    for (TestData t : testData) {
      expectedInvocations.put(t.cloudType, expectedInvocations.getOrDefault(t.cloudType, 0) + 1);
      AnsibleConfigureServers.Params params = new AnsibleConfigureServers.Params();
      buildValidParams(t, params, Universe.saveDetails(createUniverse().universeUUID,
          ApiUtils.mockUniverseUpdater(t.cloudType)));
      params.nodeName = t.node.getNodeName();
      HashMap<String, String> gflags = new HashMap<>();
      gflags.put("gflagName", "gflagValue");
      params.gflags = gflags;
      params.type = GFlags;
      params.isMasterInShellMode = true;
      params.setProperty("processType", MASTER.toString());

      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Configure, params, t.cloudType));
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Configure, params);
      verify(shellProcessHandler, times(expectedInvocations.get(t.cloudType))).run(expectedCommand,
          t.region.provider.getConfig());
    }
  }

  @Test
  public void testDestroyNodeCommandWithInvalidParam() {
    for (TestData t : testData) {
      try {
        nodeManager.nodeCommand(NodeManager.NodeCommandType.Destroy, createInvalidParams(t));
        fail();
      } catch (RuntimeException re) {
        assertThat(re.getMessage(), is("NodeTaskParams is not AnsibleDestroyServer.Params"));
      }
    }
  }

  @Test
  public void testDestroyNodeCommand() {
    Map<Common.CloudType, Integer> expectedInvocations = new HashMap<>();
    for (TestData t : testData) {
      expectedInvocations.put(t.cloudType, expectedInvocations.getOrDefault(t.cloudType, 0) + 1);
      AnsibleDestroyServer.Params params = new AnsibleDestroyServer.Params();
      buildValidParams(t, params, createUniverse());
      buildValidParams(t, params, Universe.saveDetails(createUniverse().universeUUID,
          ApiUtils.mockUniverseUpdater(t.cloudType)));

      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Destroy, params, t.cloudType));
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Destroy, params);
      verify(shellProcessHandler, times(expectedInvocations.get(t.cloudType))).run(expectedCommand,
          t.region.provider.getConfig());
    }
  }

  @Test
  public void testListNodeCommand() {
    Map<Common.CloudType, Integer> expectedInvocations = new HashMap<>();
    for (TestData t : testData) {
      expectedInvocations.put(t.cloudType, expectedInvocations.getOrDefault(t.cloudType, 0) + 1);
      NodeTaskParams params = new NodeTaskParams();
      buildValidParams(t, params, Universe.saveDetails(createUniverse().universeUUID,
          ApiUtils.mockUniverseUpdater(t.cloudType)));

      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.List, params, t.cloudType));
      nodeManager.nodeCommand(NodeManager.NodeCommandType.List, params);
      verify(shellProcessHandler, times(expectedInvocations.get(t.cloudType))).run(expectedCommand,
          t.region.provider.getConfig());
    }
  }

  @Test
  public void testControlNodeCommandWithInvalidParam() {
    for (TestData t : testData) {
      try {
        nodeManager.nodeCommand(NodeManager.NodeCommandType.Control, createInvalidParams(t));
        fail();
      } catch (RuntimeException re) {
        assertThat(re.getMessage(), is("NodeTaskParams is not AnsibleClusterServerCtl.Params"));
      }
    }
  }

  @Test
  public void testControlNodeCommand() {
    Map<Common.CloudType, Integer> expectedInvocations = new HashMap<>();
    for (TestData t : testData) {
      expectedInvocations.put(t.cloudType, expectedInvocations.getOrDefault(t.cloudType, 0) + 1);
      AnsibleClusterServerCtl.Params params = new AnsibleClusterServerCtl.Params();
      buildValidParams(t, params, Universe.saveDetails(createUniverse().universeUUID,
          ApiUtils.mockUniverseUpdater(t.cloudType)));
      params.process = "master";
      params.command = "create";

      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.Control, params, t.cloudType));
      nodeManager.nodeCommand(NodeManager.NodeCommandType.Control, params);
      verify(shellProcessHandler, times(expectedInvocations.get(t.cloudType))).run(expectedCommand,
          t.region.provider.getConfig());
    }
  }

  @Test
  public void testDockerNodeCommandWithoutDockerNetwork() {
    for (TestData t : testData) {
      NodeTaskParams params = new NodeTaskParams();
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
    Map<Common.CloudType, Integer> expectedInvocations = new HashMap<>();
    when(mockAppConfig.getString("yb.docker.network")).thenReturn(DOCKER_NETWORK);

    for (TestData t : testData) {
      expectedInvocations.put(t.cloudType, expectedInvocations.getOrDefault(t.cloudType, 0) + 1);
      NodeTaskParams params = new NodeTaskParams();
      buildValidParams(t, params, Universe.saveDetails(createUniverse().universeUUID,
          ApiUtils.mockUniverseUpdater(t.cloudType)));

      List<String> expectedCommand = t.baseCommand;
      expectedCommand.addAll(nodeCommand(NodeManager.NodeCommandType.List, params, t.cloudType));
      nodeManager.nodeCommand(NodeManager.NodeCommandType.List, params);
      verify(shellProcessHandler, times(expectedInvocations.get(t.cloudType))).run(expectedCommand,
          t.region.provider.getConfig());
    }
  }
}
