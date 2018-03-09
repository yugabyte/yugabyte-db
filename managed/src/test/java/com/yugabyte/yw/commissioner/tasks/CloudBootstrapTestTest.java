// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.models.AvailabilityZone;
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

  private void mockRegionMetadata(String regionCode) {
    Map<String, Object> regionMetadata = new HashMap<>();
    regionMetadata.put("name", "Mock Region");
    regionMetadata.put("latitude", 36.778261);
    regionMetadata.put("longitude", -119.417932);
    regionMetadata.put("ybImage", "yb-image-id");

    when(mockConfigHelper.getRegionMetadata(Common.CloudType.aws))
        .thenReturn(ImmutableMap.of(regionCode, regionMetadata));
  }

  private UUID submitTask(List<String> regionList) {
    CloudBootstrap.Params taskParams = new CloudBootstrap.Params();
    taskParams.providerCode = Common.CloudType.valueOf(defaultProvider.code);
    taskParams.providerUUID = defaultProvider.uuid;
    taskParams.regionList = regionList;
    taskParams.hostVpcId = "host-vpc-id";
    taskParams.destVpcId = "dest-vpc-id";
    return commissioner.submit(TaskType.CloudBootstrap, taskParams);
  }

  @Test
  public void testCloudBootstrapSuccess() throws InterruptedException {
    JsonNode vpcInfo = Json.parse("{\"us-west-1\": {\"zones\": {\"zone-1\": \"subnet-1\"}}}");
    mockRegionMetadata("us-west-1");
    when(mockNetworkManager.bootstrap(any(UUID.class), anyString(), anyString()))
        .thenReturn(vpcInfo);
    UUID taskUUID = submitTask(ImmutableList.of("us-west-1"));
    TaskInfo taskInfo = waitForTask(taskUUID);
    assertValue(Json.toJson(taskInfo), "taskState", "Success");
    Region r = Region.getByCode(defaultProvider, "us-west-1");
    verify(mockAWSInitializer, times(1)).initialize(defaultCustomer.uuid, defaultProvider.uuid);
    String expectedAccessKeyCode = String.format(
        "yb-%s-%s-key", defaultCustomer.code, defaultProvider.name.toLowerCase());
    verify(mockAccessManager, times(1)).addKey(r.uuid, expectedAccessKeyCode);
    Set<AvailabilityZone> zones = r.zones;
    assertNotNull(r);
    assertNotNull(zones);
    assertEquals(1, zones.size());
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
    when(mockNetworkManager.bootstrap(any(UUID.class), anyString(), anyString()))
        .thenReturn(vpcInfo);
    UUID taskUUID = submitTask(ImmutableList.of("us-west-1"));
    TaskInfo taskInfo = waitForTask(taskUUID);
    assertValue(Json.toJson(taskInfo), "taskState", "Failure");
    Region r = Region.getByCode(defaultProvider, "us-west-1");
    assertNull(r);
  }
}
