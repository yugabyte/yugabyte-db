/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.TestHelper.testDatabase;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.spy;
import static org.mockito.Mockito.when;
import static play.inject.Bindings.bind;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableList;
import com.typesafe.config.Config;
import com.yugabyte.yw.cloud.PublicCloudConstants.StorageType;
import com.yugabyte.yw.commissioner.CallHome;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.HealthChecker;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.ReleaseManager;
import com.yugabyte.yw.common.ShellProcessHandler;
import com.yugabyte.yw.common.YcqlQueryExecutor;
import com.yugabyte.yw.common.YsqlQueryExecutor;
import com.yugabyte.yw.common.alerts.AlertConfigurationWriter;
import com.yugabyte.yw.common.config.DummyRuntimeConfigFactoryImpl;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.kms.EncryptionAtRestManager;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.controllers.handlers.UniverseInfoHandler;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.metrics.MetricQueryHelper;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.KmsConfig;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.queries.QueryHelper;
import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Map;
import java.util.UUID;
import kamon.instrumentation.play.GuiceModule;
import org.apache.commons.io.FileUtils;
import org.junit.After;
import org.junit.Before;
import org.junit.Rule;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;
import org.pac4j.play.CallbackController;
import org.pac4j.play.store.PlayCacheSessionStore;
import org.pac4j.play.store.PlaySessionStore;
import org.yb.client.YBClient;
import play.Application;
import play.inject.guice.GuiceApplicationBuilder;
import play.libs.Json;
import play.modules.swagger.SwaggerModule;
import play.test.Helpers;
import play.test.WithApplication;

public class UniverseControllerTestBase extends WithApplication {
  protected static Commissioner mockCommissioner;
  protected static MetricQueryHelper mockMetricQueryHelper;

  @Rule public MockitoRule rule = MockitoJUnit.rule();

  @Mock protected play.Configuration mockAppConfig;

  private HealthChecker healthChecker;
  protected Customer customer;
  private Users user;
  protected KmsConfig kmsConfig;
  protected String authToken;
  protected YBClientService mockService;
  protected YBClient mockClient;
  protected ApiHelper mockApiHelper;
  protected CallHome mockCallHome;
  protected CustomerConfig s3StorageConfig;
  protected EncryptionAtRestManager mockEARManager;
  protected YsqlQueryExecutor mockYsqlQueryExecutor;
  protected YcqlQueryExecutor mockYcqlQueryExecutor;
  protected ShellProcessHandler mockShellProcessHandler;
  protected CallbackController mockCallbackController;
  protected PlayCacheSessionStore mockSessionStore;
  protected AlertConfigurationWriter mockAlertConfigurationWriter;
  protected Config mockRuntimeConfig;
  protected QueryHelper mockQueryHelper;
  protected ReleaseManager mockReleaseManager;

  @Override
  protected Application provideApplication() {
    mockCommissioner = mock(Commissioner.class);
    mockMetricQueryHelper = mock(MetricQueryHelper.class);
    mockClient = mock(YBClient.class);
    mockService = mock(YBClientService.class);
    mockApiHelper = mock(ApiHelper.class);
    mockCallHome = mock(CallHome.class);
    mockEARManager = mock(EncryptionAtRestManager.class);
    mockYsqlQueryExecutor = mock(YsqlQueryExecutor.class);
    mockYcqlQueryExecutor = mock(YcqlQueryExecutor.class);
    mockShellProcessHandler = mock(ShellProcessHandler.class);
    mockCallbackController = mock(CallbackController.class);
    mockSessionStore = mock(PlayCacheSessionStore.class);
    mockAlertConfigurationWriter = mock(AlertConfigurationWriter.class);
    mockRuntimeConfig = mock(Config.class);
    mockReleaseManager = mock(ReleaseManager.class);
    healthChecker = mock(HealthChecker.class);
    mockQueryHelper = mock(QueryHelper.class);

    when(mockRuntimeConfig.getBoolean("yb.cloud.enabled")).thenReturn(false);
    when(mockRuntimeConfig.getBoolean("yb.security.use_oauth")).thenReturn(false);

    return new GuiceApplicationBuilder()
        .disable(SwaggerModule.class)
        .disable(GuiceModule.class)
        .configure(testDatabase())
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
        .overrides(bind(PlaySessionStore.class).toInstance(mockSessionStore))
        .overrides(bind(play.Configuration.class).toInstance(mockAppConfig))
        .overrides(bind(AlertConfigurationWriter.class).toInstance(mockAlertConfigurationWriter))
        .overrides(
            bind(RuntimeConfigFactory.class)
                .toInstance(new DummyRuntimeConfigFactoryImpl(mockRuntimeConfig)))
        .overrides(bind(ReleaseManager.class).toInstance(mockReleaseManager))
        .overrides(bind(HealthChecker.class).toInstance(healthChecker))
        .overrides(bind(QueryHelper.class).toInstance(mockQueryHelper))
        .build();
  }

  protected PlacementInfo constructPlacementInfoObject(Map<UUID, Integer> azToNumNodesMap) {

    Map<UUID, PlacementInfo.PlacementCloud> placementCloudMap = new HashMap<>();
    Map<UUID, PlacementInfo.PlacementRegion> placementRegionMap = new HashMap<>();
    for (UUID azUUID : azToNumNodesMap.keySet()) {
      AvailabilityZone currentAz = AvailabilityZone.get(azUUID);

      // Get existing PlacementInfo Cloud or set up a new one.
      Provider currentProvider = currentAz.getProvider();
      PlacementInfo.PlacementCloud cloudItem =
          placementCloudMap.getOrDefault(currentProvider.uuid, null);
      if (cloudItem == null) {
        cloudItem = new PlacementInfo.PlacementCloud();
        cloudItem.uuid = currentProvider.uuid;
        cloudItem.code = currentProvider.code;
        cloudItem.regionList = new ArrayList<>();
        placementCloudMap.put(currentProvider.uuid, cloudItem);
      }

      // Get existing PlacementInfo Region or set up a new one.
      Region currentRegion = currentAz.region;
      PlacementInfo.PlacementRegion regionItem =
          placementRegionMap.getOrDefault(currentRegion.uuid, null);
      if (regionItem == null) {
        regionItem = new PlacementInfo.PlacementRegion();
        regionItem.uuid = currentRegion.uuid;
        regionItem.name = currentRegion.name;
        regionItem.code = currentRegion.code;
        regionItem.azList = new ArrayList<>();
        cloudItem.regionList.add(regionItem);
        placementRegionMap.put(currentRegion.uuid, regionItem);
      }

      // Get existing PlacementInfo AZ or set up a new one.
      PlacementInfo.PlacementAZ azItem = new PlacementInfo.PlacementAZ();
      azItem.name = currentAz.name;
      azItem.subnet = currentAz.subnet;
      azItem.replicationFactor = 1;
      azItem.uuid = currentAz.uuid;
      azItem.numNodesInAZ = azToNumNodesMap.get(azUUID);
      regionItem.azList.add(azItem);
    }
    PlacementInfo placementInfo = new PlacementInfo();
    placementInfo.cloudList = ImmutableList.copyOf(placementCloudMap.values());
    return placementInfo;
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
    customer = ModelFactory.testCustomer();
    s3StorageConfig = ModelFactory.createS3StorageConfig(customer, "TEST25");
    user = ModelFactory.testUser(customer);
    ObjectNode kmsConfigReq =
        Json.newObject()
            .put("name", "some config name")
            .put("base_url", "some_base_url")
            .put("api_key", "some_api_token");
    kmsConfig = ModelFactory.createKMSConfig(customer.uuid, "SMARTKEY", kmsConfigReq);
    authToken = user.createAuthToken();

    when(mockAppConfig.getString("yb.storage.path"))
        .thenReturn("/tmp/" + this.getClass().getSimpleName());

    when(mockRuntimeConfig.getString("yb.storage.path"))
        .thenReturn("/tmp/" + this.getClass().getSimpleName());
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
        return createDeviceInfo(StorageType.Premium_LRS, 1, 100, null, null, null);
      case kubernetes:
        return createDeviceInfo(null, 1, 100, null, null, null);
      default:
        throw new UnsupportedOperationException();
    }
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
}
