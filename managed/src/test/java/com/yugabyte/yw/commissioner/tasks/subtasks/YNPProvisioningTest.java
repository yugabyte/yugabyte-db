// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.lenient;
import static org.mockito.Mockito.when;
import static play.inject.Bindings.bind;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.typesafe.config.Config;
import com.typesafe.config.ConfigFactory;
import com.typesafe.config.ConfigValueFactory;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.CustomWsClientFactoryProvider;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import java.lang.reflect.Field;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.time.Duration;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;
import play.Application;

@RunWith(MockitoJUnitRunner.class)
public class YNPProvisioningTest extends FakeDBApplication {

  @Mock private BaseTaskDependencies baseTaskDependencies;

  @Mock private NodeUniverseManager nodeUniverseManager;

  @Mock private RuntimeConfGetter confGetter;

  private Customer customer;
  private Provider provider;
  private YNPProvisioning ynpProvisioning;
  private ObjectMapper objectMapper;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    provider = ModelFactory.awsProvider(customer);
    objectMapper = new ObjectMapper();

    // Set up mock behavior for CloudQueryHelper from FakeDBApplication
    lenient()
        .when(
            mockCloudQueryHelper.getDeviceNames(
                any(), any(), anyString(), any(), any(), anyString()))
        .thenReturn(List.of("/dev/sdb", "/dev/sdc"));
    when(baseTaskDependencies.getConfGetter()).thenReturn(confGetter);
    ynpProvisioning =
        new YNPProvisioning(baseTaskDependencies, mockCloudQueryHelper, mockFileHelperService);
  }

  private void setTaskParams(YNPProvisioning.Params params) throws Exception {
    // taskParams is in AbstractTaskBase, which is 3 levels up in the hierarchy:
    // YNPProvisioning -> NodeTaskBase -> UniverseDefinitionTaskBase -> UniverseTaskBase ->
    // AbstractTaskBase
    Class<?> clazz = ynpProvisioning.getClass();
    while (clazz != null && !clazz.getSimpleName().equals("AbstractTaskBase")) {
      clazz = clazz.getSuperclass();
    }
    if (clazz != null) {
      Field taskParamsField = clazz.getDeclaredField("taskParams");
      taskParamsField.setAccessible(true);
      taskParamsField.set(ynpProvisioning, params);
    } else {
      throw new RuntimeException("Could not find AbstractTaskBase class");
    }
  }

  @Override
  protected Application provideApplication() {
    // Set up mocks before building the application (provideApplication is called before @Before)
    // Use lenient() to avoid strict stubbing issues during application initialization
    // Mock getConfForScope calls
    lenient().when(confGetter.getConfForScope(any(Provider.class), any())).thenReturn("/tmp");
    lenient().when(confGetter.getConfForScope(any(Customer.class), any())).thenReturn(false);

    // Mock getGlobalConf calls needed by NodeAgentClient and other classes
    lenient()
        .when(confGetter.getGlobalConf(eq(GlobalConfKeys.nodeAgentConnectionCacheSize)))
        .thenReturn(100);
    lenient()
        .when(confGetter.getGlobalConf(eq(GlobalConfKeys.nodeAgentTokenLifetime)))
        .thenReturn(Duration.ofHours(1));
    lenient()
        .when(confGetter.getGlobalConf(eq(GlobalConfKeys.nodeAgentEnableMessageCompression)))
        .thenReturn(false);
    lenient()
        .when(confGetter.getGlobalConf(eq(GlobalConfKeys.nodeAgentConnectTimeout)))
        .thenReturn(Duration.ofSeconds(30));
    lenient()
        .when(confGetter.getGlobalConf(eq(GlobalConfKeys.nodeAgentIdleConnectionTimeout)))
        .thenReturn(Duration.ofMinutes(5));
    lenient()
        .when(confGetter.getGlobalConf(eq(GlobalConfKeys.nodeAgentConnectionKeepAliveTime)))
        .thenReturn(Duration.ofSeconds(10));
    lenient()
        .when(confGetter.getGlobalConf(eq(GlobalConfKeys.nodeAgentConnectionKeepAliveTimeout)))
        .thenReturn(Duration.ofSeconds(10));
    lenient()
        .when(confGetter.getGlobalConf(eq(GlobalConfKeys.nodeAgentDescribePollDeadline)))
        .thenReturn(Duration.ofSeconds(2));
    lenient()
        .when(confGetter.getGlobalConf(eq(GlobalConfKeys.accessLogExcludeRegex)))
        .thenReturn(Collections.emptyList());

    // Mock getGlobalConf() without parameters - needed by QueryHelper
    // Create a real Config with the required value to avoid ClassCastException
    Config realConfig =
        ConfigFactory.empty()
            .withValue(
                Util.LIVE_QUERY_TIMEOUTS,
                ConfigValueFactory.fromMap(
                    java.util.Map.of(
                        "timeout", "30s",
                        "requestTimeout", "30s",
                        "connectionTimeout", "30s")));
    lenient().doReturn(realConfig).when(confGetter).getGlobalConf();

    // Use FakeDBApplication's provideApplication which already mocks YbcUpgrade, HealthChecker,
    // etc.
    // and add our specific overrides
    // Note: CloudQueryHelper and FileHelperService are already bound by FakeDBApplication
    return super.provideApplication(
        app ->
            app.overrides(bind(RuntimeConfGetter.class).toInstance(confGetter))
                .overrides(
                    bind(com.yugabyte.yw.common.CustomWsClientFactory.class)
                        .toProvider(CustomWsClientFactoryProvider.class)));
  }

  private void verifyCommunicationPorts(JsonNode primaryRoot, Map<String, Integer> expectedPorts) {
    JsonNode portsNode = primaryRoot.get("ynp").get("communication_ports");
    for (Map.Entry<String, Integer> entry : expectedPorts.entrySet()) {
      assertEquals(
          String.valueOf(expectedPorts.get(entry.getKey())), portsNode.get(entry.getKey()));
    }
  }

  @Test
  public void testGetProvisionArgumentsWithPrimaryCluster() throws Exception {
    // Create universe with primary cluster
    Universe universe = ModelFactory.createUniverse("test-universe", customer.getId());
    Universe.saveDetails(
        universe.getUniverseUUID(), ApiUtils.mockUniverseUpdater("host", CloudType.aws));
    Map<String, Integer> expectedCommunicationPorts = new HashMap<>();
    expectedCommunicationPorts.put("master_http_port", 1);
    expectedCommunicationPorts.put("master_rpc_port", 2);
    expectedCommunicationPorts.put("tserver_http_port", 3);
    expectedCommunicationPorts.put("tserver_rpc_port", 4);
    expectedCommunicationPorts.put("yb_controller_http_port", 5);
    expectedCommunicationPorts.put("yb_controller_rpc_port", 6);
    expectedCommunicationPorts.put("ycql_server_http_port", 7);
    expectedCommunicationPorts.put("ycql_server_rpc_port", 8);
    expectedCommunicationPorts.put("ysql_server_http_port", 9);
    expectedCommunicationPorts.put("ysql_server_rpc_port", 10);
    expectedCommunicationPorts.put("node_exporter_port", 11);
    Universe.saveDetails(
        universe.getUniverseUUID(),
        u -> {
          UniverseDefinitionTaskParams details = u.getUniverseDetails();
          details.communicationPorts.masterHttpPort =
              expectedCommunicationPorts.get("master_http_port");
          details.communicationPorts.masterRpcPort =
              expectedCommunicationPorts.get("master_rpc_port");
          details.communicationPorts.tserverHttpPort =
              expectedCommunicationPorts.get("tserver_http_port");
          details.communicationPorts.tserverRpcPort =
              expectedCommunicationPorts.get("tserver_rpc_port");
          details.communicationPorts.ybControllerHttpPort =
              expectedCommunicationPorts.get("yb_controller_http_port");
          details.communicationPorts.ybControllerrRpcPort =
              expectedCommunicationPorts.get("yb_controller_rpc_port");
          details.communicationPorts.yqlServerHttpPort =
              expectedCommunicationPorts.get("ycql_server_http_port");
          details.communicationPorts.ysqlServerRpcPort =
              expectedCommunicationPorts.get("ycql_server_rpc_port");
          details.communicationPorts.ysqlServerHttpPort =
              expectedCommunicationPorts.get("ysql_server_http_port");
          details.communicationPorts.ysqlServerRpcPort =
              expectedCommunicationPorts.get("ysql_server_rpc_port");
          details.communicationPorts.nodeExporterPort =
              expectedCommunicationPorts.get("node_exporter_port");
        });

    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();

    // Get a node from primary cluster
    NodeDetails primaryNode = universe.getNodes().iterator().next();
    UUID primaryClusterUuid = universeDetails.getPrimaryCluster().uuid;
    assertEquals(primaryClusterUuid, primaryNode.placementUuid);

    // Set up task params
    YNPProvisioning.Params params = new YNPProvisioning.Params();
    params.setUniverseUUID(universe.getUniverseUUID());
    params.nodeName = primaryNode.nodeName;
    params.isYbPrebuiltImage = false;
    params.deviceInfo = new DeviceInfo();
    params.deviceInfo.numVolumes = 2;
    setTaskParams(params);

    // Set up node with cloud info
    primaryNode.cloudInfo = new CloudSpecificInfo();
    primaryNode.cloudInfo.cloud = "aws";
    primaryNode.cloudInfo.private_ip = "10.0.0.1";
    primaryNode.cloudInfo.region = "us-west-2";
    primaryNode.cloudInfo.instance_type = "m5.large";

    // Set primary cluster user intent with clockbound
    UserIntent primaryUserIntent = universeDetails.getPrimaryCluster().userIntent;
    primaryUserIntent.setUseClockbound(true);
    primaryUserIntent.providerType = CloudType.aws;
    primaryUserIntent.provider = provider.getUuid().toString();
    // Set device info on user intent (required for getDeviceInfoForNode)
    primaryUserIntent.deviceInfo = new DeviceInfo();
    primaryUserIntent.deviceInfo.numVolumes = 2;

    // Create temp file for output
    Path tempFile = Files.createTempFile("ynp-test-primary-", ".json");
    String outputPath = tempFile.toString();
    Path nodeAgentHome = Paths.get("/tmp/node-agent");

    // Call the method
    ynpProvisioning.generateProvisionConfig(
        universe, primaryNode, provider, outputPath, nodeAgentHome);

    // Verify the JSON file was created and contains expected data
    assertTrue(Files.exists(tempFile));
    JsonNode rootNode = objectMapper.readTree(Files.readAllBytes(tempFile));

    // Verify ynp node
    JsonNode ynpNode = rootNode.get("ynp");
    assertNotNull(ynpNode);
    assertEquals("10.0.0.1", ynpNode.get("node_ip").asText());
    assertEquals(false, ynpNode.get("is_install_node_agent").asBoolean());
    assertEquals(true, ynpNode.get("is_configure_clockbound").asBoolean());

    // Verify extra node
    JsonNode extraNode = rootNode.get("extra");
    assertNotNull(extraNode);
    assertEquals("aws", extraNode.get("cloud_type").asText());

    // Clean up
    Files.deleteIfExists(tempFile);
  }

  @Test
  public void testGetProvisionArgumentsWithReadReplicaCluster() throws Exception {
    // Create universe with primary cluster
    Universe universe = ModelFactory.createUniverse("test-universe", customer.getId());
    Universe.saveDetails(
        universe.getUniverseUUID(), ApiUtils.mockUniverseUpdater("host", CloudType.aws));

    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();

    // Set up primary cluster user intent
    UserIntent primaryUserIntent = universeDetails.getPrimaryCluster().userIntent;
    primaryUserIntent.setUseClockbound(false); // Different from read replica
    primaryUserIntent.providerType = CloudType.aws;
    primaryUserIntent.provider = provider.getUuid().toString();
    // Set device info on primary user intent
    primaryUserIntent.deviceInfo = new DeviceInfo();
    primaryUserIntent.deviceInfo.numVolumes = 2;

    // Add read replica cluster
    UserIntent rrUserIntent = primaryUserIntent.clone();
    rrUserIntent.setUseClockbound(true); // Read replica has clockbound enabled
    // Set device info on read replica user intent
    rrUserIntent.deviceInfo = new DeviceInfo();
    rrUserIntent.deviceInfo.numVolumes = 3;
    PlacementInfo placementInfo = new PlacementInfo();
    PlacementInfo.PlacementAZ placementAZ = new PlacementInfo.PlacementAZ();
    placementAZ.uuid = UUID.randomUUID();
    placementAZ.numNodesInAZ = 1;
    placementAZ.replicationFactor = 1;
    List<PlacementInfo.PlacementAZ> azList = new ArrayList<>();
    azList.add(placementAZ);
    PlacementInfo.PlacementRegion placementRegion = new PlacementInfo.PlacementRegion();
    placementRegion.azList = azList;
    List<PlacementInfo.PlacementRegion> regionList = new ArrayList<>();
    regionList.add(placementRegion);
    PlacementInfo.PlacementCloud placementCloud = new PlacementInfo.PlacementCloud();
    placementCloud.uuid = provider.getUuid();
    placementCloud.regionList = regionList;
    List<PlacementInfo.PlacementCloud> cloudList = new ArrayList<>();
    cloudList.add(placementCloud);
    placementInfo.cloudList = cloudList;

    Universe.saveDetails(
        universe.getUniverseUUID(),
        ApiUtils.mockUniverseUpdaterWithReadReplica(rrUserIntent, placementInfo));

    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    universeDetails = universe.getUniverseDetails();

    // Ensure deviceInfo is set on read replica cluster
    universeDetails.getReadOnlyClusters().get(0).userIntent.deviceInfo = new DeviceInfo();
    universeDetails.getReadOnlyClusters().get(0).userIntent.deviceInfo.numVolumes = 3;

    // Verify read replica cluster exists
    List<UniverseDefinitionTaskParams.Cluster> readOnlyClusters =
        universeDetails.getReadOnlyClusters();
    assertTrue("Read replica cluster should exist", readOnlyClusters.size() > 0);
    UUID rrClusterUuid = readOnlyClusters.get(0).uuid;

    // Get a node from read replica cluster
    NodeDetails rrNode = null;
    for (NodeDetails node : universe.getNodes()) {
      if (rrClusterUuid.equals(node.placementUuid)) {
        rrNode = node;
        break;
      }
    }
    assertNotNull("Read replica node should exist", rrNode);

    // Set up task params
    YNPProvisioning.Params params = new YNPProvisioning.Params();
    params.setUniverseUUID(universe.getUniverseUUID());
    params.nodeName = rrNode.nodeName;
    params.isYbPrebuiltImage = true;
    params.deviceInfo = new DeviceInfo();
    params.deviceInfo.numVolumes = 3;
    setTaskParams(params);

    // Set up node with cloud info
    rrNode.cloudInfo = new CloudSpecificInfo();
    rrNode.cloudInfo.cloud = "aws";
    rrNode.cloudInfo.private_ip = "10.0.0.2";
    rrNode.cloudInfo.region = "us-east-1";
    rrNode.cloudInfo.instance_type = "m5.xlarge";

    // Create temp file for output
    Path tempFile = Files.createTempFile("ynp-test-rr-", ".json");
    String outputPath = tempFile.toString();
    Path nodeAgentHome = Paths.get("/tmp/node-agent");

    // Call the method
    ynpProvisioning.generateProvisionConfig(universe, rrNode, provider, outputPath, nodeAgentHome);

    // Verify the JSON file was created and contains expected data
    assertTrue(Files.exists(tempFile));
    JsonNode rootNode = objectMapper.readTree(Files.readAllBytes(tempFile));

    // Verify ynp node - should use read replica cluster's user intent
    JsonNode ynpNode = rootNode.get("ynp");
    assertNotNull(ynpNode);
    assertEquals("10.0.0.2", ynpNode.get("node_ip").asText());
    assertEquals(true, ynpNode.get("is_yb_prebuilt_image").asBoolean());
    // Note: Currently line 84 uses getPrimaryCluster() which is a bug,
    // but the test verifies the actual behavior
    // The clockbound value should come from the read replica cluster's user intent
    // but currently it uses primary cluster's user intent (bug)
    assertEquals(true, ynpNode.get("is_configure_clockbound").asBoolean());

    // Verify extra node
    JsonNode extraNode = rootNode.get("extra");
    assertNotNull(extraNode);
    assertEquals("aws", extraNode.get("cloud_type").asText());

    // Verify mount paths are generated based on read replica cluster's device info
    assertTrue(extraNode.has("mount_paths"));
    String mountPaths = extraNode.get("mount_paths").asText();
    // Should have 3 mount paths for 3 volumes
    assertTrue(mountPaths.contains("/mnt/d0"));
    assertTrue(mountPaths.contains("/mnt/d1"));
    assertTrue(mountPaths.contains("/mnt/d2"));

    // Clean up
    Files.deleteIfExists(tempFile);
  }

  @Test
  public void testGetProvisionArgumentsWithBothClusters() throws Exception {
    // Create universe with primary cluster
    Universe universe = ModelFactory.createUniverse("test-universe", customer.getId());
    Universe.saveDetails(
        universe.getUniverseUUID(), ApiUtils.mockUniverseUpdater("host", CloudType.aws));

    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();

    // Set up primary cluster user intent
    UserIntent primaryUserIntent = universeDetails.getPrimaryCluster().userIntent;
    primaryUserIntent.setUseClockbound(false);
    primaryUserIntent.providerType = CloudType.aws;
    primaryUserIntent.provider = provider.getUuid().toString();
    // Set device info on primary user intent
    primaryUserIntent.deviceInfo = new DeviceInfo();
    primaryUserIntent.deviceInfo.numVolumes = 2;

    // Add read replica cluster
    UserIntent rrUserIntent = primaryUserIntent.clone();
    rrUserIntent.setUseClockbound(true);
    // Set device info on read replica user intent
    rrUserIntent.deviceInfo = new DeviceInfo();
    rrUserIntent.deviceInfo.numVolumes = 3;
    PlacementInfo placementInfo = new PlacementInfo();
    PlacementInfo.PlacementAZ placementAZ = new PlacementInfo.PlacementAZ();
    placementAZ.uuid = UUID.randomUUID();
    placementAZ.numNodesInAZ = 1;
    placementAZ.replicationFactor = 1;
    List<PlacementInfo.PlacementAZ> azList = new ArrayList<>();
    azList.add(placementAZ);
    PlacementInfo.PlacementRegion placementRegion = new PlacementInfo.PlacementRegion();
    placementRegion.azList = azList;
    List<PlacementInfo.PlacementRegion> regionList = new ArrayList<>();
    regionList.add(placementRegion);
    PlacementInfo.PlacementCloud placementCloud = new PlacementInfo.PlacementCloud();
    placementCloud.uuid = provider.getUuid();
    placementCloud.regionList = regionList;
    List<PlacementInfo.PlacementCloud> cloudList = new ArrayList<>();
    cloudList.add(placementCloud);
    placementInfo.cloudList = cloudList;

    Universe.saveDetails(
        universe.getUniverseUUID(),
        ApiUtils.mockUniverseUpdaterWithReadReplica(rrUserIntent, placementInfo));

    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    universeDetails = universe.getUniverseDetails();

    // Ensure deviceInfo is set on both clusters
    universeDetails.getPrimaryCluster().userIntent.deviceInfo = new DeviceInfo();
    universeDetails.getPrimaryCluster().userIntent.deviceInfo.numVolumes = 2;
    universeDetails.getReadOnlyClusters().get(0).userIntent.deviceInfo = new DeviceInfo();
    universeDetails.getReadOnlyClusters().get(0).userIntent.deviceInfo.numVolumes = 3;

    UUID primaryClusterUuid = universeDetails.getPrimaryCluster().uuid;
    UUID rrClusterUuid = universeDetails.getReadOnlyClusters().get(0).uuid;

    // Test with primary cluster node
    NodeDetails primaryNode = null;
    for (NodeDetails node : universe.getNodes()) {
      if (primaryClusterUuid.equals(node.placementUuid)) {
        primaryNode = node;
        break;
      }
    }
    assertNotNull("Primary node should exist", primaryNode);

    primaryNode.cloudInfo = new CloudSpecificInfo();
    primaryNode.cloudInfo.cloud = "aws";
    primaryNode.cloudInfo.private_ip = "10.0.0.10";
    primaryNode.cloudInfo.region = "us-west-2";
    primaryNode.cloudInfo.instance_type = "m5.large";

    YNPProvisioning.Params params = new YNPProvisioning.Params();
    params.setUniverseUUID(universe.getUniverseUUID());
    params.nodeName = primaryNode.nodeName;
    params.isYbPrebuiltImage = false;
    params.deviceInfo = new DeviceInfo();
    params.deviceInfo.numVolumes = 2;
    setTaskParams(params);

    Path tempFilePrimary = Files.createTempFile("ynp-test-primary-", ".json");
    Path nodeAgentHome = Paths.get("/tmp/node-agent");

    ynpProvisioning.generateProvisionConfig(
        universe, primaryNode, provider, tempFilePrimary.toString(), nodeAgentHome);

    JsonNode primaryRoot = objectMapper.readTree(Files.readAllBytes(tempFilePrimary));
    assertEquals("10.0.0.10", primaryRoot.get("ynp").get("node_ip").asText());
    assertEquals(false, primaryRoot.get("ynp").get("is_configure_clockbound").asBoolean());

    // Test with read replica cluster node
    NodeDetails rrNode = null;
    for (NodeDetails node : universe.getNodes()) {
      if (rrClusterUuid.equals(node.placementUuid)) {
        rrNode = node;
        break;
      }
    }
    assertNotNull("Read replica node should exist", rrNode);

    rrNode.cloudInfo = new CloudSpecificInfo();
    rrNode.cloudInfo.cloud = "aws";
    rrNode.cloudInfo.private_ip = "10.0.0.20";
    rrNode.cloudInfo.region = "us-east-1";
    rrNode.cloudInfo.instance_type = "m5.xlarge";

    params.deviceInfo.numVolumes = 3;
    params.nodeName = rrNode.nodeName;
    setTaskParams(params);

    Path tempFileRR = Files.createTempFile("ynp-test-rr-", ".json");

    ynpProvisioning.generateProvisionConfig(
        universe, rrNode, provider, tempFileRR.toString(), nodeAgentHome);

    JsonNode rrRoot = objectMapper.readTree(Files.readAllBytes(tempFileRR));
    assertEquals("10.0.0.20", rrRoot.get("ynp").get("node_ip").asText());
    // Note: This currently shows the bug - it uses primary cluster's clockbound setting
    // instead of read replica's setting
    assertEquals(true, rrRoot.get("ynp").get("is_configure_clockbound").asBoolean());

    // Clean up
    Files.deleteIfExists(tempFilePrimary);
    Files.deleteIfExists(tempFileRR);
  }
}
