// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyMap;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.cloud.PublicCloudConstants;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.tasks.CommissionerBaseTest;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.HashMap;
import java.util.Map;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import org.yb.client.YBClient;

@RunWith(MockitoJUnitRunner.class)
public class ValidateGFlagsTest extends CommissionerBaseTest {

  private Universe defaultUniverse;
  private YBClient mockClient;
  private AvailabilityZone az1;
  private AvailabilityZone az2;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    defaultUniverse = ModelFactory.createUniverse(defaultCustomer.getId());

    Region region = Region.create(defaultProvider, "region-1", "Region 1", "yb-image-1");
    az1 = AvailabilityZone.createOrThrow(region, "az-1", "AZ 1", "subnet-1");
    az2 = AvailabilityZone.createOrThrow(region, "az-2", "AZ 2", "subnet-2");

    UniverseDefinitionTaskParams.UserIntent userIntent =
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent;
    DeviceInfo deviceInfo = new DeviceInfo();
    deviceInfo.numVolumes = 1;
    deviceInfo.volumeSize = 100;
    deviceInfo.mountPoints = "/mnt/d0";
    deviceInfo.storageType = PublicCloudConstants.StorageType.GP2;
    userIntent.deviceInfo = deviceInfo;
    defaultUniverse.save();

    mockClient = mock(YBClient.class);
    when(mockYBClient.getUniverseClient(any())).thenReturn(mockClient);
    when(mockGFlagsValidation.validateGFlags(any(YBClient.class), anyMap(), any(ServerType.class)))
        .thenReturn(new HashMap<>());
  }

  // Testing that for 2 AZs each having 1 master and 1 tserver, validation was called 4 times.
  @Test
  public void testValidationCountForTwoAZs() throws Exception {
    UniverseDefinitionTaskParams details = defaultUniverse.getUniverseDetails();
    details.getPrimaryCluster().userIntent.ybSoftwareVersion = "2025.1.0.0-b168";
    details.nodeDetailsSet.clear();
    details.nodeDetailsSet.add(createNode("node-1", az1, true, true));
    details.nodeDetailsSet.add(createNode("node-2", az2, true, true));
    defaultUniverse.setUniverseDetails(details);
    defaultUniverse.save();

    ValidateGFlags.Params params = new ValidateGFlags.Params();
    params.setUniverseUUID(defaultUniverse.getUniverseUUID());
    params.ybSoftwareVersion = "2025.1.0.0-b168";
    params.useCLIBinary = false;

    ValidateGFlags task = AbstractTaskBase.createTask(ValidateGFlags.class);
    task.initialize(params);
    task.run();

    verify(mockGFlagsValidation, times(4))
        .validateGFlags(any(YBClient.class), anyMap(), any(ServerType.class));
  }

  // Test that nodes with null cloudInfo and null cloudInfo.private_ip are skipped during gflags
  // validation.
  @Test
  public void testValidationSkippedForNodeWithNullCloudInfo() throws Exception {
    UniverseDefinitionTaskParams details = defaultUniverse.getUniverseDetails();
    UniverseDefinitionTaskParams.Cluster primaryCluster = details.getPrimaryCluster();
    primaryCluster.userIntent.ybSoftwareVersion = "2025.1.0.0-b168";
    details.nodeDetailsSet.clear();

    NodeDetails node1 = new NodeDetails();
    node1.nodeName = "node-1";
    node1.azUuid = az1.getUuid();
    node1.placementUuid = primaryCluster.uuid;
    node1.isMaster = true;
    node1.isTserver = true;
    node1.cloudInfo = null;

    NodeDetails node2 = new NodeDetails();
    node2.nodeName = "node-2";
    node2.azUuid = az2.getUuid();
    node2.placementUuid = primaryCluster.uuid;
    node2.isMaster = true;
    node2.isTserver = true;
    node2.cloudInfo = new CloudSpecificInfo();
    node2.cloudInfo.private_ip = null;

    details.nodeDetailsSet.add(node1);
    details.nodeDetailsSet.add(node2);
    defaultUniverse.setUniverseDetails(details);
    defaultUniverse.save();

    ValidateGFlags.Params params = new ValidateGFlags.Params();
    params.setUniverseUUID(defaultUniverse.getUniverseUUID());
    params.ybSoftwareVersion = "2025.1.0.0-b168";
    params.useCLIBinary = false;

    ValidateGFlags task = AbstractTaskBase.createTask(ValidateGFlags.class);
    task.initialize(params);
    task.run();

    verify(mockGFlagsValidation, never())
        .validateGFlags(any(YBClient.class), anyMap(), any(ServerType.class));
  }

  // Sample negative case - exception should be thrown by subtask if invalid gflag was given.
  @Test
  public void testValidationFailsWithInvalidGFlags() throws Exception {
    UniverseDefinitionTaskParams details = defaultUniverse.getUniverseDetails();
    details.getPrimaryCluster().userIntent.ybSoftwareVersion = "2025.1.0.0-b168";
    details.nodeDetailsSet.clear();
    details.nodeDetailsSet.add(createNode("node-1", az1, true, false));
    defaultUniverse.setUniverseDetails(details);
    defaultUniverse.save();

    Map<String, String> validationErrors = new HashMap<>();
    validationErrors.put("invalid_flag", "Error validating flag");
    when(mockGFlagsValidation.validateGFlags(any(YBClient.class), anyMap(), any(ServerType.class)))
        .thenReturn(validationErrors);

    ValidateGFlags.Params params = new ValidateGFlags.Params();
    params.setUniverseUUID(defaultUniverse.getUniverseUUID());
    params.ybSoftwareVersion = "2025.1.0.0-b168";
    params.useCLIBinary = false;

    ValidateGFlags task = AbstractTaskBase.createTask(ValidateGFlags.class);
    task.initialize(params);

    PlatformServiceException exception =
        assertThrows(PlatformServiceException.class, () -> task.run());
    assertEquals(BAD_REQUEST, exception.getHttpStatus());
  }

  private NodeDetails createNode(
      String nodeName, AvailabilityZone az, boolean isMaster, boolean isTserver) {
    NodeDetails node = new NodeDetails();
    node.nodeName = nodeName;
    node.azUuid = az.getUuid();
    node.isMaster = isMaster;
    node.isTserver = isTserver;
    node.cloudInfo = new CloudSpecificInfo();
    node.cloudInfo.private_ip = "10.0.0.1";
    node.placementUuid = defaultUniverse.getUniverseDetails().getPrimaryCluster().uuid;
    return node;
  }
}
