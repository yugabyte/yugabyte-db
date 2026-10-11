/*
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.TestHelper.testDatabase;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.lenient;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.reset;
import static org.mockito.Mockito.spy;
import static org.mockito.Mockito.when;
import static play.inject.Bindings.bind;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.typesafe.config.Config;
import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.cloud.PublicCloudConstants.StorageType;
import com.yugabyte.yw.commissioner.CallHome;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.HealthChecker;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.CloudUtilFactory;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.CustomWsClientFactory;
import com.yugabyte.yw.common.CustomWsClientFactoryProvider;
import com.yugabyte.yw.common.KubernetesManager;
import com.yugabyte.yw.common.KubernetesManagerFactory;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.PlatformGuiceApplicationBaseTest;
import com.yugabyte.yw.common.ReleaseContainer;
import com.yugabyte.yw.common.ReleaseManager;
import com.yugabyte.yw.common.ReleasesUtils;
import com.yugabyte.yw.common.ShellProcessHandler;
import com.yugabyte.yw.common.SoftwareUpgradeHelper;
import com.yugabyte.yw.common.YcqlQueryExecutor;
import com.yugabyte.yw.common.YsqlQueryExecutor;
import com.yugabyte.yw.common.alerts.AlertConfigurationWriter;
import com.yugabyte.yw.common.config.DummyRuntimeConfigFactoryImpl;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import com.yugabyte.yw.common.gflags.GFlagsValidation;
import com.yugabyte.yw.common.kms.EncryptionAtRestManager;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.metrics.MetricQueryHelper;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.KmsConfig;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.YugawareProperty;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.queries.QueryHelper;
import java.io.File;
import java.io.IOException;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import kamon.instrumentation.play.GuiceModule;
import org.apache.commons.io.FileUtils;
import org.junit.After;
import org.junit.Before;
import org.junit.Rule;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;
import org.pac4j.core.context.session.SessionStore;
import org.pac4j.play.CallbackController;
import org.pac4j.play.LogoutController;
import org.pac4j.play.store.PlayCacheSessionStore;
import org.yb.client.YBClientApi;
import play.Application;
import play.inject.guice.GuiceApplicationBuilder;
import play.libs.Json;

public class UniverseControllerTestBase extends PlatformGuiceApplicationBaseTest {
  protected static Commissioner mockCommissioner;
  protected static MetricQueryHelper mockMetricQueryHelper;

  @Rule public MockitoRule rule = MockitoJUnit.rule();

  @Mock RuntimeConfigFactory mockRuntimeConfigFactory;
  @Mock RuntimeConfGetter mockConfGetter;

  private HealthChecker healthChecker;
  protected Customer customer;
  protected Users user;
  protected KmsConfig kmsConfig;
  protected String authToken;
  protected YBClientService mockService;
  protected YBClientApi mockClient;
  protected YsqlQueryExecutor mockYsqlQueryExecutor;
  protected ApiHelper mockApiHelper;
  protected CallHome mockCallHome;
  protected CustomerConfig s3StorageConfig;
  protected EncryptionAtRestManager mockEARManager;
  protected YcqlQueryExecutor mockYcqlQueryExecutor;
  protected ShellProcessHandler mockShellProcessHandler;
  protected CallbackController mockCallbackController;
  protected PlayCacheSessionStore mockSessionStore;
  protected LogoutController mockLogoutController;
  protected AlertConfigurationWriter mockAlertConfigurationWriter;
  protected Config mockRuntimeConfig;
  protected QueryHelper mockQueryHelper;
  protected ReleaseManager mockReleaseManager;
  protected RuntimeConfigFactory runtimeConfigFactory;
  protected ReleaseContainer mockReleaseContainer;
  protected ReleaseManager.ReleaseMetadata mockReleaseMetadata;
  protected ReleaseManager.ReleaseMetadata mockYbcReleaseMetadata;
  protected KubernetesManagerFactory kubernetesManagerFactory;
  protected CloudUtilFactory mockCloudUtilFactory;
  protected ReleasesUtils mockReleasesUtils;
  protected GFlagsValidation mockGFlagsValidation;
  protected SoftwareUpgradeHelper mockSoftwareUpgradeHelper;

  protected GuiceApplicationBuilder appOverrides(GuiceApplicationBuilder applicationBuilder) {
    return applicationBuilder;
  }

  // This hierarchy's application wiring (the mock bindings below) is identical for every test
  // method, so share one application instance across the class' methods. Per-method isolation is
  // restored by the base class (DB truncation + reflective mock reset) and by re-applying
  // per-method
  // stubbings in setUp(). Subclasses that override appOverrides() with method-varying bindings must
  // override this to return false.
  @Override
  protected boolean reusableApplication() {
    return true;
  }

  @Override
  protected Application provideApplication() {
    mockCommissioner = mock(Commissioner.class);
    mockMetricQueryHelper = mock(MetricQueryHelper.class);
    mockClient = mock(YBClientApi.class);
    mockService = mock(YBClientService.class);
    mockApiHelper = mock(ApiHelper.class);
    mockCallHome = mock(CallHome.class);
    mockEARManager = mock(EncryptionAtRestManager.class);
    mockYsqlQueryExecutor = mock(YsqlQueryExecutor.class);
    mockYcqlQueryExecutor = mock(YcqlQueryExecutor.class);
    mockShellProcessHandler = mock(ShellProcessHandler.class);
    mockCallbackController = mock(CallbackController.class);
    mockSessionStore = mock(PlayCacheSessionStore.class);
    mockLogoutController = mock(LogoutController.class);
    mockAlertConfigurationWriter = mock(AlertConfigurationWriter.class);
    mockRuntimeConfig = mock(Config.class);
    mockReleaseManager = mock(ReleaseManager.class);
    healthChecker = mock(HealthChecker.class);
    mockQueryHelper = mock(QueryHelper.class);
    kubernetesManagerFactory = mock(KubernetesManagerFactory.class);
    mockCloudUtilFactory = mock(CloudUtilFactory.class);
    mockReleasesUtils = mock(ReleasesUtils.class);
    mockGFlagsValidation = mock(GFlagsValidation.class);
    mockSoftwareUpgradeHelper = mock(SoftwareUpgradeHelper.class);

    // Behavioral stubbing lives in stubMockRuntimeConfig(), invoked from setUp() so it is
    // re-applied
    // per method: the application is reused across the class' methods, and the base class resets
    // all
    // mock instances between methods (Mockito.reset), which would otherwise wipe these stubs.
    stubMockRuntimeConfig();

    return appOverrides(new GuiceApplicationBuilder())
        .disable(GuiceModule.class)
        .configure(testDatabase())
        .configure("yb.storage.path", "/tmp/" + this.getClass().getSimpleName())
        .overrides(bind(YBClientService.class).toInstance(mockService))
        .overrides(bind(Commissioner.class).toInstance(mockCommissioner))
        .overrides(bind(MetricQueryHelper.class).toInstance(mockMetricQueryHelper))
        .overrides(bind(ApiHelper.class).toInstance(mockApiHelper))
        .overrides(bind(CallHome.class).toInstance(mockCallHome))
        .overrides(bind(EncryptionAtRestManager.class).toInstance(mockEARManager))
        .overrides(bind(YsqlQueryExecutor.class).toInstance(mockYsqlQueryExecutor))
        .overrides(bind(YcqlQueryExecutor.class).toInstance(mockYcqlQueryExecutor))
        .overrides(bind(ShellProcessHandler.class).toInstance(mockShellProcessHandler))
        .overrides(bind(CallbackController.class).toInstance(mockCallbackController))
        .overrides(bind(SessionStore.class).toInstance(mockSessionStore))
        .overrides(bind(LogoutController.class).toInstance(mockLogoutController))
        .overrides(bind(AlertConfigurationWriter.class).toInstance(mockAlertConfigurationWriter))
        .overrides(
            bind(RuntimeConfigFactory.class)
                .toInstance(new DummyRuntimeConfigFactoryImpl(mockRuntimeConfig)))
        .overrides(bind(ReleaseManager.class).toInstance(mockReleaseManager))
        .overrides(bind(HealthChecker.class).toInstance(healthChecker))
        .overrides(bind(QueryHelper.class).toInstance(mockQueryHelper))
        .overrides(bind(KubernetesManagerFactory.class).toInstance(kubernetesManagerFactory))
        .overrides(bind(SoftwareUpgradeHelper.class).toInstance(mockSoftwareUpgradeHelper))
        .overrides(
            bind(CustomWsClientFactory.class).toProvider(CustomWsClientFactoryProvider.class))
        .overrides(bind(GFlagsValidation.class).toInstance(mockGFlagsValidation))
        .build();
  }

  // Applied both before building the (reused) application - so startup reads see them - and again
  // in
  // setUp() for every method, because the base class resets all mock instances between methods.
  // Marked lenient() because most keys are only consumed during application startup (which happens
  // once for the whole reused class), so a given method need not exercise every stub.
  private void stubMockRuntimeConfig() {
    lenient().when(mockRuntimeConfig.getString("yb.metrics.scrape_interval")).thenReturn("10s");
    lenient().when(mockRuntimeConfig.getBoolean("yb.cloud.enabled")).thenReturn(false);
    lenient().when(mockRuntimeConfig.getBoolean("yb.security.use_oauth")).thenReturn(false);
    lenient()
        .when(mockRuntimeConfig.getInt("yb.fs_stateless.max_files_count_persist"))
        .thenReturn(100);
    lenient().when(mockRuntimeConfig.getBoolean("yb.fs_stateless.suppress_error")).thenReturn(true);
    lenient().when(mockRuntimeConfig.getInt("yb.max_volume_count")).thenReturn(32);
    lenient()
        .when(mockRuntimeConfig.getLong("yb.fs_stateless.max_file_size_bytes"))
        .thenReturn((long) 10000);
    lenient()
        .when(mockRuntimeConfig.getString("yb.storage.path"))
        .thenReturn("/tmp/" + this.getClass().getSimpleName());
    lenient().when(mockRuntimeConfig.getString("yb.filepaths.tmpDirectory")).thenReturn("/tmp");
    lenient()
        .when(mockRuntimeConfig.getBoolean("yb.ui.feature_flags.enable_earlyoom"))
        .thenReturn(true);
    lenient().when(mockRuntimeConfigFactory.globalRuntimeConf()).thenReturn(mockRuntimeConfig);
    lenient().when(kubernetesManagerFactory.getManager()).thenReturn(mock(KubernetesManager.class));
  }

  static boolean areConfigObjectsEqual(ArrayNode nodeDetailSet, Map<UUID, Integer> azToNodeMap) {
    for (JsonNode nodeDetail : nodeDetailSet) {
      UUID azUUID = UUID.fromString(nodeDetail.get("azUuid").asText());
      azToNodeMap.put(azUUID, azToNodeMap.getOrDefault(azUUID, 0) - 1);
    }
    return !azToNodeMap.values().removeIf(nodeDifference -> nodeDifference != 0);
  }

  @Before
  public void setUp() {
    // Re-apply the runtime-config stubs on the reused mock instances (the base class reset them).
    stubMockRuntimeConfig();
    // These two are static (so the base class' per-method mock reset skips them). Under a reused
    // application they retain invocations from previous methods, which breaks per-method
    // verify(...) counts, so reset them explicitly before each method.
    reset(mockCommissioner, mockMetricQueryHelper);
    UUID yugawareUuid = UUID.randomUUID();
    ObjectNode ywMetadata = Json.newObject();
    ywMetadata.put("yugaware_uuid", yugawareUuid.toString());
    ywMetadata.put("version", "2024.2.0.0-b1");
    YugawareProperty.addConfigProperty(
        ConfigHelper.ConfigType.YugawareMetadata.name(), ywMetadata, "Yugaware Metadata");

    mockYsqlQueryExecutor = app.injector().instanceOf(YsqlQueryExecutor.class);
    YsqlQueryExecutor.ConsistencyInfoResp consistencyInfo =
        new YsqlQueryExecutor.ConsistencyInfoResp();
    try {
      java.lang.reflect.Field ywUuidField =
          YsqlQueryExecutor.ConsistencyInfoResp.class.getDeclaredField("ywUuid");
      ywUuidField.setAccessible(true);
      ywUuidField.set(consistencyInfo, yugawareUuid);
    } catch (Exception e) {
      throw new RuntimeException("Failed to set yw_uuid in consistency info", e);
    }
    lenient().when(mockYsqlQueryExecutor.getConsistencyInfo(any())).thenReturn(consistencyInfo);

    customer = ModelFactory.testCustomer();
    s3StorageConfig = ModelFactory.createS3StorageConfig(customer, "TEST25");
    user = ModelFactory.testUser(customer);
    ObjectNode kmsConfigReq =
        Json.newObject()
            .put("name", "some config name")
            .put("base_url", "some_base_url")
            .put("api_key", "some_api_token");
    kmsConfig = ModelFactory.createKMSConfig(customer.getUuid(), "SMARTKEY", kmsConfigReq);
    authToken = user.createAuthToken();
    runtimeConfigFactory = app.injector().instanceOf(SettableRuntimeConfigFactory.class);

    mockReleaseContainer =
        spy(
            new ReleaseContainer(
                mockReleaseMetadata, mockCloudUtilFactory, mockRuntimeConfig, mockReleasesUtils));
    when(mockReleaseManager.getReleaseByVersion(any())).thenReturn(mockReleaseContainer);
    doReturn("/opt/yugabyte/releases/2.17.4.0-b10/yb-2.17.4.0-b10-linux-x86_64.tar.gz")
        .when(mockReleaseContainer)
        .getFilePath(Architecture.x86_64);
    // when(mockReleaseMetadata.getFilePath(any()))
    //     .thenReturn("/opt/yugabyte/releases/2.17.4.0-b10/yb-2.17.4.0-b10-linux-x86_64.tar.gz");

    mockYbcReleaseMetadata = spy(new ReleaseManager.ReleaseMetadata());
    mockYbcReleaseMetadata.filePath =
        "/opt/yugabyte/ybc/releases/1.0.0-b18/ybc-1.0.0-b18-linux-x86_64.tar.gz";
    when(mockReleaseManager.getYbcReleaseByVersion(any(), any(), any()))
        .thenReturn(mockYbcReleaseMetadata);
    doReturn("/opt/yugabyte/ybc/releases/1.0.0-b18/ybc-1.0.0-b18-linux-x86_64.tar.gz")
        .when(mockYbcReleaseMetadata)
        .getFilePath(Architecture.x86_64);
    // when(mockYbcReleaseMetadata.getFilePath(any()))
    //     .thenReturn("/opt/yugabyte/ybc/releases/1.0.0-b18/ybc-1.0.0-b18-linux-x86_64.tar.gz");
  }

  @After
  public void tearDown() throws IOException {
    FileUtils.deleteDirectory(new File("/tmp/" + this.getClass().getSimpleName() + "/certs"));
  }

  // Change the node state to removed, for one of the nodes in the given universe uuid.
  protected void setInTransitNode(UUID universeUUID) {
    Universe.UniverseUpdater updater =
        universe -> {
          UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
          NodeDetails node = universeDetails.nodeDetailsSet.iterator().next();
          node.state = NodeState.Removed;
          universe.setUniverseDetails(universeDetails);
        };
    Universe.saveDetails(universeUUID, updater);
  }

  protected ObjectNode createValidDeviceInfo(CloudType cloudType) {
    switch (cloudType) {
      case aws:
        return createDeviceInfo(StorageType.GP2, 1, 100, null, null, null);
      case gcp:
        return createDeviceInfo(StorageType.Persistent, 1, 100, null, null, null);
      case azu:
        return createDeviceInfo(StorageType.PremiumV2_LRS, 1, 100, null, null, null);
      case kubernetes:
        return createDeviceInfo(null, 1, 100, null, null, null);
      default:
        throw new UnsupportedOperationException();
    }
  }

  protected ArrayNode clustersArray(ObjectNode userIntentJson, ObjectNode placementInfoJson) {
    ObjectNode cluster = Json.newObject();
    cluster.set("userIntent", userIntentJson);
    cluster.set("placementInfo", placementInfoJson);
    return Json.newArray().add(cluster);
  }

  protected Set<NodeDetails> buildNodeDetailsSet(
      PlacementInfo placementInfo, UUID clusterUuid, NodeState nodeState, String instanceType) {
    Set<NodeDetails> nodes = new HashSet<>();
    int idx = 1;
    for (PlacementInfo.PlacementCloud cloud : placementInfo.cloudList) {
      for (PlacementInfo.PlacementRegion region : cloud.regionList) {
        for (PlacementInfo.PlacementAZ az : region.azList) {
          for (int n = 0; n < az.numNodesInAZ; n++) {
            NodeDetails node = ApiUtils.getDummyNodeDetails(idx++, nodeState);
            node.placementUuid = clusterUuid;
            node.azUuid = az.uuid;
            AvailabilityZone availabilityZone = AvailabilityZone.getOrBadRequest(az.uuid);
            node.cloudInfo.region = availabilityZone.getRegion().getCode();
            node.cloudInfo.az = availabilityZone.getName();
            if (instanceType != null) {
              node.cloudInfo.instance_type = instanceType;
            }
            if (nodeState == NodeState.ToBeAdded) {
              node.nodeName = null;
            }
            nodes.add(node);
          }
        }
      }
    }
    return nodes;
  }

  protected ArrayNode nodeDetailsSetJson(
      PlacementInfo placementInfo, UUID clusterUuid, NodeState nodeState, String instanceType) {
    ArrayNode nodeDetailsJsonArray = Json.newArray();
    buildNodeDetailsSet(placementInfo, clusterUuid, nodeState, instanceType)
        .forEach(node -> nodeDetailsJsonArray.add(Json.toJson(node)));
    return nodeDetailsJsonArray;
  }

  protected ArrayNode nodeDetailsSetJson(PlacementInfo placementInfo, UUID clusterUuid) {
    return nodeDetailsSetJson(placementInfo, clusterUuid, NodeState.ToBeAdded, null);
  }

  protected UUID setClustersAndNodeDetailsSet(
      ObjectNode bodyJson, ObjectNode userIntentJson, PlacementInfo placementInfo) {
    return setClustersAndNodeDetailsSet(bodyJson, userIntentJson, placementInfo, null);
  }

  protected UUID setClustersAndNodeDetailsSet(
      ObjectNode bodyJson,
      ObjectNode userIntentJson,
      PlacementInfo placementInfo,
      String instanceType) {
    UUID clusterUuid = UUID.randomUUID();
    ObjectNode clusterJson = Json.newObject();
    clusterJson.set("userIntent", userIntentJson);
    clusterJson.set("placementInfo", Json.toJson(placementInfo));
    clusterJson.put("uuid", clusterUuid.toString());
    bodyJson.set("clusters", Json.newArray().add(clusterJson));
    bodyJson.set(
        "nodeDetailsSet",
        nodeDetailsSetJson(placementInfo, clusterUuid, NodeState.ToBeAdded, instanceType));
    return clusterUuid;
  }

  protected ObjectNode createDeviceInfo(
      StorageType storageType,
      Integer numVolumes,
      Integer volumeSize,
      Integer diskIops,
      Integer throughput,
      String mountPoints) {
    ObjectNode deviceInfo = Json.newObject();
    if (storageType != null) {
      deviceInfo.put("storageType", storageType.name());
    }
    if (volumeSize != null) {
      deviceInfo.put("volumeSize", volumeSize);
    }
    if (numVolumes != null) {
      deviceInfo.put("numVolumes", numVolumes);
    }
    if (diskIops != null) {
      deviceInfo.put("diskIops", diskIops);
    }
    if (throughput != null) {
      deviceInfo.put("throughput", throughput);
    }
    if (mountPoints != null) {
      deviceInfo.put("mountPoints", mountPoints);
    }
    return deviceInfo;
  }

  // Creating basic RF3 placement with total replicas always equal to 3.
  protected PlacementInfo placement(Provider provider, int numberOfZones) {
    assert numberOfZones <= 3;
    PlacementInfo placementInfo = new PlacementInfo();
    if (numberOfZones == 0) {
      return placementInfo;
    }
    int baseReplicas = 3 / numberOfZones;
    int extraReplicas = 3 % numberOfZones;
    for (int i = 1; i < numberOfZones + 1; i++) {
      AvailabilityZone az = AvailabilityZone.getByCode(provider, "az-" + i);
      int replicas = baseReplicas + (i == 1 ? extraReplicas : 0);
      PlacementInfoUtil.addPlacementZone(az.getUuid(), placementInfo, replicas, replicas);
    }
    return placementInfo;
  }

  protected PlacementInfo placement(Provider provider) {
    return placement(provider, 3);
  }
}
