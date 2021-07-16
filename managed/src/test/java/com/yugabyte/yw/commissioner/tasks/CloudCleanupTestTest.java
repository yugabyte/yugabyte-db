// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.models.*;
import com.yugabyte.yw.models.helpers.TaskType;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.runners.MockitoJUnitRunner;
import play.libs.Json;

import java.util.List;
import java.util.Optional;
import java.util.UUID;

import static com.yugabyte.yw.common.AssertHelper.assertValue;
import static org.junit.Assert.*;
import static org.mockito.Mockito.*;

@RunWith(MockitoJUnitRunner.class)
public class CloudCleanupTestTest extends CommissionerBaseTest {
  @InjectMocks Commissioner commissioner;

  private UUID submitTask(List<String> regionList) {
    CloudCleanup.Params taskParams = new CloudCleanup.Params();
    taskParams.providerUUID = defaultProvider.uuid;
    taskParams.regionList = regionList;
    return commissioner.submit(TaskType.CloudCleanup, taskParams);
  }

  private void assertRegionZones(String regionCode, List<String> zones, boolean exists) {
    Region r = Region.getByCode(defaultProvider, regionCode);
    if (exists) {
      assertNotNull(r);
    } else {
      assertNull(r);
    }

    zones.forEach(
        zone -> {
          Optional<AvailabilityZone> az = AvailabilityZone.maybeGetByCode(defaultProvider, zone);
          if (exists) {
            assertTrue(az.isPresent());
          } else {
            assertFalse(az.isPresent());
          }
        });
  }

  private void assertAccessKeyAndProvider(boolean exists) {
    List<AccessKey> accessKeyList = AccessKey.getAll(defaultProvider.uuid);
    defaultProvider = Provider.get(defaultProvider.uuid);
    if (exists) {
      assertFalse(accessKeyList.isEmpty());
      assertNotNull(defaultProvider);
    } else {
      assertTrue(accessKeyList.isEmpty());
      assertNull(defaultProvider);
    }
  }

  @Test
  public void testCloudCleanupSuccess() throws InterruptedException {
    Region region = Region.create(defaultProvider, "us-west-1", "us west 1", "yb-image");
    AvailabilityZone.createOrThrow(region, "az-1", "az 1", "subnet-1");
    AvailabilityZone.createOrThrow(region, "az-2", "az 2", "subnet-2");
    JsonNode vpcInfo = Json.parse("{\"us-west-1\": \"VPC Deleted\"}");
    when(mockNetworkManager.cleanupOrFail(region.uuid)).thenReturn(vpcInfo);
    UUID taskUUID = submitTask(ImmutableList.of("us-west-1"));
    TaskInfo taskInfo = waitForTask(taskUUID);
    verify(mockAccessManager, times(1)).deleteKey(region.uuid, "yb-amazon-key");
    assertValue(Json.toJson(taskInfo), "taskState", "Success");
    assertRegionZones("us-west-1", ImmutableList.of("az-1", "az-2"), false);
    assertAccessKeyAndProvider(false);
  }

  @Test
  public void testCloudCleanupWithMultipleRegions() throws InterruptedException {
    Region region1 = Region.create(defaultProvider, "us-west-1", "us west 1", "yb-image");
    AvailabilityZone.createOrThrow(region1, "az-1", "az 1", "subnet-1");
    AvailabilityZone.createOrThrow(region1, "az-2", "az 2", "subnet-2");
    Region region2 = Region.create(defaultProvider, "us-west-2", "us west 2", "yb-image");
    AvailabilityZone.createOrThrow(region2, "az-3", "az 3", "subnet-3");
    AvailabilityZone.createOrThrow(region2, "az-4", "az 4", "subnet-4");
    AccessKey.create(defaultProvider.uuid, "access-key", new AccessKey.KeyInfo());
    JsonNode vpcInfo = Json.parse("{\"us-west-1\": \"VPC Deleted\"}");
    when(mockNetworkManager.cleanupOrFail(region1.uuid)).thenReturn(vpcInfo);
    UUID taskUUID = submitTask(ImmutableList.of("us-west-1"));
    TaskInfo taskInfo = waitForTask(taskUUID);
    verify(mockAccessManager, times(1)).deleteKey(region1.uuid, "yb-amazon-key");
    assertValue(Json.toJson(taskInfo), "taskState", "Success");
    assertRegionZones("us-west-1", ImmutableList.of("az-1", "az-2"), false);
    assertRegionZones("us-west-2", ImmutableList.of("az-3", "az-4"), true);
    assertAccessKeyAndProvider(true);
  }

  @Test
  public void testCloudCleanupInvalidRegion() throws InterruptedException {
    UUID taskUUID = submitTask(ImmutableList.of("fake-region"));
    TaskInfo taskInfo = waitForTask(taskUUID);
    assertValue(Json.toJson(taskInfo), "taskState", "Failure");
  }

  @Test
  public void testCloudCleanupError() throws InterruptedException {
    Region region = Region.create(defaultProvider, "us-west-1", "us west 1", "yb-image");
    JsonNode vpcInfo = Json.parse("{\"error\": \"Something failed\"}");
    when(mockNetworkManager.cleanupOrFail(region.uuid)).thenReturn(vpcInfo);
    UUID taskUUID = submitTask(ImmutableList.of(region.code));
    TaskInfo taskInfo = waitForTask(taskUUID);
    assertValue(Json.toJson(taskInfo), "taskState", "Failure");
  }
}
