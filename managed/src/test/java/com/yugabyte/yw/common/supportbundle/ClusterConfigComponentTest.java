// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundle;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.mockito.Mockito.doThrow;
import static org.mockito.Mockito.spy;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.protobuf.ByteString;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.SupportBundleUtil;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.PlacementInfo.PlacementCloud;
import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Paths;
import java.util.Date;
import java.util.Map;
import org.apache.commons.io.FileUtils;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;
import org.yb.WireProtocol.AppStatusPB;
import org.yb.WireProtocol.AppStatusPB.ErrorCode;
import org.yb.client.GetMasterClusterConfigResponse;
import org.yb.client.YBClientApi;
import org.yb.master.CatalogEntityInfo.EncryptionInfoPB;
import org.yb.master.CatalogEntityInfo.SysClusterConfigEntryPB;
import org.yb.master.MasterTypes.MasterErrorPB;
import org.yb.master.MasterTypes.MasterErrorPB.Code;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class ClusterConfigComponentTest extends FakeDBApplication {
  @Mock public YBClientService mockYbClientService;
  @Mock public YBClientApi mockClient;

  private Universe universe;
  private Customer customer;
  private final SupportBundleUtil supportBundleUtil = new SupportBundleUtil();
  private final String fakeSupportBundleBasePath =
      "/tmp/yugaware_tests/support_bundle-cluster_config/";
  private final String fakeBundlePath = fakeSupportBundleBasePath + "yb-support-bundle-test-logs";

  @Before
  public void setUp() throws Exception {
    this.customer = ModelFactory.testCustomer();
    this.universe = ModelFactory.createUniverse(customer.getId());

    PlacementInfo placementInfo = new PlacementInfo();
    PlacementCloud cloud = new PlacementCloud();
    cloud.code = "aws";
    placementInfo.cloudList.add(cloud);
    this.universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            (u) -> {
              UniverseDefinitionTaskParams details = u.getUniverseDetails();
              details.getPrimaryCluster().placementInfo = placementInfo;
              u.setUniverseDetails(details);
            });

    when(mockYbClientService.getUniverseClient(universe)).thenReturn(mockClient);
  }

  @After
  public void tearDown() throws IOException {
    FileUtils.deleteDirectory(new File(fakeSupportBundleBasePath));
  }

  @Test
  public void testDownloadComponentSavesClusterConfigAndPlacements() throws Exception {
    SysClusterConfigEntryPB config =
        SysClusterConfigEntryPB.newBuilder()
            .setVersion(7)
            .setClusterUuid("test-cluster-uuid")
            .build();
    when(mockClient.getMasterClusterConfig())
        .thenReturn(new GetMasterClusterConfigResponse(0, "", config, null));

    ClusterConfigComponent component =
        new ClusterConfigComponent(mockYbClientService, supportBundleUtil);
    component.downloadComponent(null, customer, universe, Paths.get(fakeBundlePath), null);

    File clusterConfigFile = new File(fakeBundlePath + "/ClusterConfig/cluster_config.json");
    File placementsFile = new File(fakeBundlePath + "/ClusterConfig/placement_info.json");
    assertTrue(clusterConfigFile.exists());
    assertTrue(placementsFile.exists());

    JsonNode configJson =
        Json.parse(FileUtils.readFileToString(clusterConfigFile, StandardCharsets.UTF_8));
    assertEquals(7, configJson.get("version").asInt());
    assertEquals("test-cluster-uuid", configJson.get("clusterUuid").asText());

    JsonNode placementsJson =
        Json.parse(FileUtils.readFileToString(placementsFile, StandardCharsets.UTF_8));
    assertEquals(1, placementsJson.size());
    assertEquals(
        universe.getUniverseDetails().getPrimaryCluster().uuid.toString(),
        placementsJson.get(0).get("uuid").asText());
    assertEquals("PRIMARY", placementsJson.get(0).get("clusterType").asText());
    assertEquals(
        "aws",
        placementsJson.get(0).get("placementInfo").get("cloudList").get(0).get("code").asText());
  }

  @Test
  public void testDownloadComponentRedactsUniverseKeyRegistry() throws Exception {
    SysClusterConfigEntryPB config =
        SysClusterConfigEntryPB.newBuilder()
            .setVersion(1)
            .setEncryptionInfo(
                EncryptionInfoPB.newBuilder()
                    .setEncryptionEnabled(true)
                    .setUniverseKeyRegistryEncoded(ByteString.copyFromUtf8("super-secret-key"))
                    .build())
            .build();
    when(mockClient.getMasterClusterConfig())
        .thenReturn(new GetMasterClusterConfigResponse(0, "", config, null));

    ClusterConfigComponent component =
        new ClusterConfigComponent(mockYbClientService, supportBundleUtil);
    component.downloadComponent(null, customer, universe, Paths.get(fakeBundlePath), null);

    String configJson =
        FileUtils.readFileToString(
            new File(fakeBundlePath + "/ClusterConfig/cluster_config.json"),
            StandardCharsets.UTF_8);
    assertFalse(configJson.contains("super-secret-key"));
    assertTrue(
        configJson.contains("encryptionEnabled") || configJson.contains("encryption_enabled"));
  }

  @Test
  public void testDownloadComponentSavesPlacementsWhenMasterConfigFails() throws Exception {
    when(mockClient.getMasterClusterConfig()).thenThrow(new RuntimeException("master unreachable"));

    ClusterConfigComponent component =
        new ClusterConfigComponent(mockYbClientService, supportBundleUtil);
    component.downloadComponent(null, customer, universe, Paths.get(fakeBundlePath), null);

    assertFalse(new File(fakeBundlePath + "/ClusterConfig/cluster_config.json").exists());
    assertTrue(new File(fakeBundlePath + "/ClusterConfig/placement_info.json").exists());
  }

  @Test
  public void testDownloadComponentSavesClusterConfigWhenPlacementsFail() throws Exception {
    mockSuccessfulClusterConfig(7, "test-cluster-uuid");
    ClusterConfigComponent component =
        spy(new ClusterConfigComponent(mockYbClientService, supportBundleUtil));
    doThrow(new RuntimeException("placement failed")).when(component).buildPlacementsJson(universe);

    component.downloadComponent(null, customer, universe, Paths.get(fakeBundlePath), null);

    assertTrue(new File(fakeBundlePath + "/ClusterConfig/cluster_config.json").exists());
    assertFalse(new File(fakeBundlePath + "/ClusterConfig/placement_info.json").exists());
  }

  @Test
  public void testDownloadComponentBetweenDatesDelegatesToDownloadComponent() throws Exception {
    mockSuccessfulClusterConfig(3, "between-dates-uuid");
    ClusterConfigComponent component =
        new ClusterConfigComponent(mockYbClientService, supportBundleUtil);

    component.downloadComponentBetweenDates(
        null, customer, universe, Paths.get(fakeBundlePath), new Date(), new Date(), null);

    assertTrue(new File(fakeBundlePath + "/ClusterConfig/cluster_config.json").exists());
    assertTrue(new File(fakeBundlePath + "/ClusterConfig/placement_info.json").exists());
  }

  @Test
  public void testDownloadComponentSkipsClusterConfigWhenMasterReturnsError() throws Exception {
    MasterErrorPB error =
        MasterErrorPB.newBuilder()
            .setCode(Code.UNKNOWN_ERROR)
            .setStatus(
                AppStatusPB.newBuilder()
                    .setCode(ErrorCode.RUNTIME_ERROR)
                    .setMessage("config error")
                    .build())
            .build();
    when(mockClient.getMasterClusterConfig())
        .thenReturn(new GetMasterClusterConfigResponse(0, "", null, error));

    ClusterConfigComponent component =
        new ClusterConfigComponent(mockYbClientService, supportBundleUtil);
    component.downloadComponent(null, customer, universe, Paths.get(fakeBundlePath), null);

    assertFalse(new File(fakeBundlePath + "/ClusterConfig/cluster_config.json").exists());
    assertTrue(new File(fakeBundlePath + "/ClusterConfig/placement_info.json").exists());
  }

  @Test
  public void testGetFilesListWithSizesIncludesPlacementAndClusterConfig() throws Exception {
    mockSuccessfulClusterConfig(7, "test-cluster-uuid");
    ClusterConfigComponent component =
        new ClusterConfigComponent(mockYbClientService, supportBundleUtil);

    Map<String, Long> sizes =
        component.getFilesListWithSizes(customer, null, universe, null, null, null);

    long expected =
        component.buildPlacementsJson(universe).toPrettyString().length()
            + component.fetchClusterConfigJson(universe).toPrettyString().length();
    assertEquals(1, sizes.size());
    assertEquals(expected, sizes.get(ClusterConfigComponent.CLUSTER_CONFIG_FOLDER).longValue());
  }

  @Test
  public void testGetFilesListWithSizesFallsBackWhenMasterConfigFails() throws Exception {
    when(mockClient.getMasterClusterConfig()).thenThrow(new RuntimeException("master unreachable"));
    ClusterConfigComponent component =
        new ClusterConfigComponent(mockYbClientService, supportBundleUtil);

    Map<String, Long> sizes =
        component.getFilesListWithSizes(customer, null, universe, null, null, null);

    long expected = component.buildPlacementsJson(universe).toPrettyString().length() + 20_000;
    assertEquals(expected, sizes.get(ClusterConfigComponent.CLUSTER_CONFIG_FOLDER).longValue());
  }

  @Test
  public void testGetFilesListWithSizesContinuesWhenPlacementsFail() throws Exception {
    mockSuccessfulClusterConfig(7, "test-cluster-uuid");
    ClusterConfigComponent component =
        spy(new ClusterConfigComponent(mockYbClientService, supportBundleUtil));
    doThrow(new RuntimeException("placement failed")).when(component).buildPlacementsJson(universe);

    Map<String, Long> sizes =
        component.getFilesListWithSizes(customer, null, universe, null, null, null);

    long expected = component.fetchClusterConfigJson(universe).toPrettyString().length();
    assertEquals(expected, sizes.get(ClusterConfigComponent.CLUSTER_CONFIG_FOLDER).longValue());
  }

  @Test
  public void testBuildPlacementsJsonIncludesEachCluster() {
    PlacementInfo replicaPlacement = new PlacementInfo();
    PlacementCloud replicaCloud = new PlacementCloud();
    replicaCloud.code = "gcp";
    replicaPlacement.cloudList.add(replicaCloud);
    UniverseDefinitionTaskParams.Cluster replica =
        new UniverseDefinitionTaskParams.Cluster(
            UniverseDefinitionTaskParams.ClusterType.ASYNC,
            universe.getUniverseDetails().getPrimaryCluster().userIntent);
    replica.placementInfo = replicaPlacement;
    universe.getUniverseDetails().clusters.add(replica);

    ClusterConfigComponent component =
        new ClusterConfigComponent(mockYbClientService, supportBundleUtil);
    JsonNode placements = component.buildPlacementsJson(universe);

    assertEquals(2, placements.size());
    assertEquals("PRIMARY", placements.get(0).get("clusterType").asText());
    assertEquals("ASYNC", placements.get(1).get("clusterType").asText());
    assertEquals(
        "gcp", placements.get(1).get("placementInfo").get("cloudList").get(0).get("code").asText());
  }

  private void mockSuccessfulClusterConfig(int version, String clusterUuid) throws Exception {
    SysClusterConfigEntryPB config =
        SysClusterConfigEntryPB.newBuilder()
            .setVersion(version)
            .setClusterUuid(clusterUuid)
            .build();
    when(mockClient.getMasterClusterConfig())
        .thenReturn(new GetMasterClusterConfigResponse(0, "", config, null));
  }
}
