// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.models.TaskInfo.State.Failure;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.List;
import java.util.Optional;
import java.util.UUID;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class CloudCleanupTestTest extends CommissionerBaseTest {

  private UUID submitTask(List<String> regionList) {
    CloudCleanup.Params taskParams = new CloudCleanup.Params();
    taskParams.providerUUID = defaultProvider.getUuid();
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
    List<AccessKey> accessKeyList = AccessKey.getAll(defaultProvider.getUuid());
    defaultProvider = Provider.get(defaultProvider.getUuid());
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
    when(mockNetworkManager.cleanupOrFail(region.getUuid())).thenReturn(vpcInfo);
    UUID taskUUID = submitTask(ImmutableList.of("us-west-1"));
    TaskInfo taskInfo = waitForTask(taskUUID);
    verify(mockAccessManager, times(1)).deleteKey(region.getUuid(), "yb-amazon-key");
    assertEquals(Success, taskInfo.getTaskState());
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
    AccessKey.create(defaultProvider.getUuid(), "access-key", new AccessKey.KeyInfo());
    JsonNode vpcInfo = Json.parse("{\"us-west-1\": \"VPC Deleted\"}");
    when(mockNetworkManager.cleanupOrFail(region1.getUuid())).thenReturn(vpcInfo);
    UUID taskUUID = submitTask(ImmutableList.of("us-west-1"));
    TaskInfo taskInfo = waitForTask(taskUUID);
    verify(mockAccessManager, times(1)).deleteKey(region1.getUuid(), "yb-amazon-key");
    assertEquals(Success, taskInfo.getTaskState());
    assertRegionZones("us-west-1", ImmutableList.of("az-1", "az-2"), false);
    assertRegionZones("us-west-2", ImmutableList.of("az-3", "az-4"), true);
    assertAccessKeyAndProvider(true);
  }

  @Test
  public void testCloudCleanupInvalidRegion() throws InterruptedException {
    UUID taskUUID = submitTask(ImmutableList.of("fake-region"));
    TaskInfo taskInfo = waitForTask(taskUUID);
    assertEquals(Failure, taskInfo.getTaskState());
  }

  @Test
  public void testCloudCleanupError() throws InterruptedException {
    Region region = Region.create(defaultProvider, "us-west-1", "us west 1", "yb-image");
    JsonNode vpcInfo = Json.parse("{\"error\": \"Something failed\"}");
    when(mockNetworkManager.cleanupOrFail(region.getUuid())).thenReturn(vpcInfo);
    UUID taskUUID = submitTask(ImmutableList.of(region.getCode()));
    TaskInfo taskInfo = waitForTask(taskUUID);
    assertEquals(Failure, taskInfo.getTaskState());
  }
}
