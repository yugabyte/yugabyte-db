// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.api.v2.mappers;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;

import api.v2.mappers.UserIntentMapper;
import api.v2.models.ClusterNodeSpec;
import api.v2.models.PerProcessNodeSpec;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.PerProcessDetails;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntentOverrides;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import java.util.HashMap;
import java.util.Map;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class UserIntentMapperTest {

  @Test
  public void testNullUserIntentReturnsNull() {
    assertNull(UserIntentMapper.INSTANCE.userIntentToClusterNodeSpec(null));
  }

  @Test
  public void testPerProcessOverridesMapToMasterAndTserverNodeSpec() {
    UserIntent userIntent = new UserIntent();
    userIntent.dedicatedNodes = true;
    userIntent.instanceType = "c5.xlarge";
    userIntent.deviceInfo = ApiUtils.getDummyDeviceInfo(1, 100);

    PerProcessDetails masterDetails = new PerProcessDetails();
    masterDetails.setInstanceType("c5.4xlarge");
    masterDetails.setDeviceInfo(ApiUtils.getDummyDeviceInfo(1, 50));

    PerProcessDetails tserverDetails = new PerProcessDetails();
    tserverDetails.setInstanceType("c5.2xlarge");
    tserverDetails.setDeviceInfo(ApiUtils.getDummyDeviceInfo(2, 200));

    Map<ServerType, PerProcessDetails> perProcess = new HashMap<>();
    perProcess.put(ServerType.MASTER, masterDetails);
    perProcess.put(ServerType.TSERVER, tserverDetails);

    UserIntentOverrides overrides = new UserIntentOverrides();
    overrides.setPerProcess(perProcess);
    userIntent.setUserIntentOverrides(overrides);

    ClusterNodeSpec clusterNodeSpec =
        UserIntentMapper.INSTANCE.userIntentToClusterNodeSpec(userIntent);

    assertNotNull(clusterNodeSpec.getMaster());
    assertEquals("c5.4xlarge", clusterNodeSpec.getMaster().getInstanceType());
    assertEquals(Integer.valueOf(50), clusterNodeSpec.getMaster().getStorageSpec().getVolumeSize());

    assertNotNull(clusterNodeSpec.getTserver());
    assertEquals("c5.2xlarge", clusterNodeSpec.getTserver().getInstanceType());
    assertEquals(
        Integer.valueOf(200), clusterNodeSpec.getTserver().getStorageSpec().getVolumeSize());
    assertEquals(Integer.valueOf(2), clusterNodeSpec.getTserver().getStorageSpec().getNumVolumes());
  }

  @Test
  public void testLegacyMasterFieldsMapToMasterNodeSpec() {
    UserIntent userIntent = new UserIntent();
    userIntent.dedicatedNodes = true;
    userIntent.instanceType = "c5.xlarge";
    userIntent.deviceInfo = ApiUtils.getDummyDeviceInfo(1, 100);
    userIntent.masterInstanceType = "m5.2xlarge";
    userIntent.masterDeviceInfo = ApiUtils.getDummyDeviceInfo(1, 75);

    ClusterNodeSpec clusterNodeSpec =
        UserIntentMapper.INSTANCE.userIntentToClusterNodeSpec(userIntent);

    PerProcessNodeSpec masterNodeSpec = clusterNodeSpec.getMaster();
    assertNotNull(masterNodeSpec);
    assertEquals("m5.2xlarge", masterNodeSpec.getInstanceType());
    assertEquals(Integer.valueOf(75), masterNodeSpec.getStorageSpec().getVolumeSize());
    assertNull(clusterNodeSpec.getTserver());
  }

  @Test
  public void testLegacyMasterFieldsOverridePerProcessMasterSpec() {
    UserIntent userIntent = new UserIntent();
    userIntent.dedicatedNodes = true;
    userIntent.instanceType = "c5.xlarge";
    userIntent.deviceInfo = ApiUtils.getDummyDeviceInfo(1, 100);

    PerProcessDetails masterDetails = new PerProcessDetails();
    masterDetails.setInstanceType("c5.4xlarge");
    masterDetails.setDeviceInfo(ApiUtils.getDummyDeviceInfo(1, 50));

    Map<ServerType, PerProcessDetails> perProcess = new HashMap<>();
    perProcess.put(ServerType.MASTER, masterDetails);

    UserIntentOverrides overrides = new UserIntentOverrides();
    overrides.setPerProcess(perProcess);
    userIntent.setUserIntentOverrides(overrides);

    userIntent.masterInstanceType = "m5.2xlarge";
    DeviceInfo masterDeviceInfo = ApiUtils.getDummyDeviceInfo(1, 75);
    userIntent.masterDeviceInfo = masterDeviceInfo;

    ClusterNodeSpec clusterNodeSpec =
        UserIntentMapper.INSTANCE.userIntentToClusterNodeSpec(userIntent);

    PerProcessNodeSpec masterNodeSpec = clusterNodeSpec.getMaster();
    assertNotNull(masterNodeSpec);
    assertEquals("m5.2xlarge", masterNodeSpec.getInstanceType());
    assertEquals(Integer.valueOf(75), masterNodeSpec.getStorageSpec().getVolumeSize());
  }
}
