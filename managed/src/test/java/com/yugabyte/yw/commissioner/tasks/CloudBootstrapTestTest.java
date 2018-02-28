// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.runners.MockitoJUnitRunner;
import play.libs.Json;

import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;

import static com.yugabyte.yw.common.AssertHelper.assertValue;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.mockito.Mockito.*;

@RunWith(MockitoJUnitRunner.class)
public class CloudBootstrapTestTest extends CommissionerBaseTest {
  @InjectMocks
  Commissioner commissioner;

  final String hostVpcRegion = "host-vpc-region";
  final String hostVpcId = "host-vpc-id";
  final String destVpcId = "dest-vpc-id";

  private void mockRegionMetadata(Common.CloudType cloudType, String regionCode) {
    Map<String, Object> regionMetadata = new HashMap<>();
    regionMetadata.put("name", "Mock Region");
    regionMetadata.put("latitude", 36.778261);
    regionMetadata.put("longitude", -119.417932);
    regionMetadata.put("ybImage", "yb-image-id");

    when(mockConfigHelper.getRegionMetadata(cloudType))
        .thenReturn(ImmutableMap.of(regionCode, regionMetadata));
  }

  private UUID submitTask(List<String> regionList) {
    return submitTask(regionList, defaultProvider);
  }

  private UUID submitTask(List<String> regionList, Provider provider) {
    CloudBootstrap.Params taskParams = new CloudBootstrap.Params();
    taskParams.providerCode = Common.CloudType.valueOf(provider.code);
    taskParams.providerUUID = provider.uuid;
    taskParams.regionList = regionList;
    taskParams.hostVpcRegion = hostVpcRegion;
    taskParams.hostVpcId = hostVpcId;
    taskParams.destVpcId = destVpcId;
    return commissioner.submit(TaskType.CloudBootstrap, taskParams);
  }

  private void validateCloudBootstrapSuccess(Provider provider, String regionName, JsonNode vpcInfo,
      JsonNode zoneInfo) throws InterruptedException {
    mockRegionMetadata(Common.CloudType.valueOf(provider.code), regionName);
    when(mockNetworkManager.bootstrap(any(UUID.class), any(UUID.class), anyString(), anyString(), anyString()))
        .thenReturn(vpcInfo);
    when(mockCloudQueryHelper.getZones(any(UUID.class), anyString()))
        .thenReturn(zoneInfo);
    UUID taskUUID = submitTask(ImmutableList.of(regionName), provider);
    TaskInfo taskInfo = waitForTask(taskUUID);
    assertValue(Json.toJson(taskInfo), "taskState", "Success");
    Region r = Region.getByCode(provider, regionName);
    // TODO: this could use a test refactor to be more multi-cloud friendly.
    if (provider.code.equals("aws")) {
      verify(mockAWSInitializer, times(1)).initialize(defaultCustomer.uuid, provider.uuid);
    } else {
      verify(mockGCPInitializer, times(1)).initialize(defaultCustomer.uuid, provider.uuid);
    }
    verify(mockNetworkManager, times(1)).bootstrap(
        null, provider.uuid, hostVpcRegion, hostVpcId, destVpcId);
    String expectedAccessKeyCode = String.format(
        "yb-%s-%s-key", defaultCustomer.code, provider.name.toLowerCase());
    verify(mockAccessManager, times(1)).addKey(r.uuid, expectedAccessKeyCode);
    Set<AvailabilityZone> zones = r.zones;
    assertNotNull(r);
    assertNotNull(zones);
    assertEquals(1, zones.size());
  }

  @Test
  public void testCloudBootstrapSuccessAws() throws InterruptedException {
    JsonNode vpcInfo = Json.parse("{\"us-west-1\": {\"zones\": {\"zone-1\": \"subnet-1\"}}}");
    JsonNode zoneInfo = Json.parse("{\"us-west-1\": {\"zone-1\": \"subnet-1\"}}");
    validateCloudBootstrapSuccess(defaultProvider, "us-west-1", vpcInfo, zoneInfo);
  }

  @Test
  public void testCloudBootstrapSuccessGcp() throws InterruptedException {
    JsonNode vpcInfo = Json.parse("{\"us-west1\": {\"zones\": [\"zone-1\"], \"subnetworks\": [\"subnet-0\"]}}");
    validateCloudBootstrapSuccess(gcpProvider, "us-west1", vpcInfo, vpcInfo);
  }

  @Test
  public void testCloudBootstrapWithInvalidRegion() throws InterruptedException {
    UUID taskUUID = submitTask(ImmutableList.of("fake-region"));
    TaskInfo taskInfo = waitForTask(taskUUID);
    assertValue(Json.toJson(taskInfo), "taskState", "Failure");
  }

  @Test
  public void testCloudBootstrapWithExistingRegion() throws InterruptedException {
    Region.create(defaultProvider, "us-west-1", "US west 1", "yb-image");
    UUID taskUUID = submitTask(ImmutableList.of("us-west-1"));
    TaskInfo taskInfo = waitForTask(taskUUID);
    assertValue(Json.toJson(taskInfo), "taskState", "Failure");
  }

  @Test
  public void testCloudBootstrapWithNetworkBootstrapError() throws InterruptedException {
    JsonNode vpcInfo = Json.parse("{\"error\": \"Something failed\"}");
    when(mockNetworkManager.bootstrap(any(UUID.class), any(UUID.class), anyString(), anyString(), anyString()))
        .thenReturn(vpcInfo);
    UUID taskUUID = submitTask(ImmutableList.of("us-west-1"));
    TaskInfo taskInfo = waitForTask(taskUUID);
    assertValue(Json.toJson(taskInfo), "taskState", "Failure");
    Region r = Region.getByCode(defaultProvider, "us-west-1");
    assertNull(r);
  }
}
