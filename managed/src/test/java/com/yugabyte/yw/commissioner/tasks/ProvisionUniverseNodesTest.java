// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertThrows;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.lenient;

import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableSet;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.forms.ProvisionUniverseNodesParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.ProviderDetails;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class ProvisionUniverseNodesTest extends CommissionerBaseTest {

  private Universe defaultUniverse;

  @Before
  public void setUp() {
    Region region = Region.create(defaultProvider, "region-1", "Region 1", "yb-image-1");
    AvailabilityZone.createOrThrow(region, "az-1", "AZ 1", "subnet-1");
    UserIntent userIntent = new UserIntent();
    userIntent.numNodes = 3;
    userIntent.provider = defaultProvider.getUuid().toString();
    userIntent.providerType = Common.CloudType.aws;
    userIntent.ybSoftwareVersion = "yb-version";
    userIntent.accessKeyCode = "demo-access";
    userIntent.replicationFactor = 3;
    userIntent.regionList = ImmutableList.of(region.getUuid());
    defaultUniverse = createUniverse(defaultCustomer.getId());
    Universe.saveDetails(
        defaultUniverse.getUniverseUUID(),
        ApiUtils.mockUniverseUpdater(userIntent, true /* setMasters */));
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    lenient()
        .when(mockNodeManager.nodeCommand(any(), any()))
        .thenReturn(ShellResponse.create(ShellResponse.ERROR_CODE_SUCCESS, "{}"));
  }

  private TaskInfo submitTask(ProvisionUniverseNodesParams taskParams) {
    try {
      UUID taskUUID = commissioner.submit(TaskType.ProvisionUniverseNodes, taskParams);
      return waitForTask(taskUUID);
    } catch (InterruptedException e) {
      throw new RuntimeException(e);
    }
  }

  private ProvisionUniverseNodesParams createTaskParams() {
    ProvisionUniverseNodesParams params = new ProvisionUniverseNodesParams();
    params.setUniverseUUID(defaultUniverse.getUniverseUUID());
    params.clusters = defaultUniverse.getUniverseDetails().clusters;
    return params;
  }

  private void setupOnPremUniverse(boolean skipProvisioning) {
    Region onPremRegion =
        Region.create(onPremProvider, "onprem-region-1", "OnPrem Region 1", "yb-image-1");
    AvailabilityZone.createOrThrow(onPremRegion, "onprem-az-1", "OnPrem AZ 1", "subnet-1");
    ProviderDetails details = onPremProvider.getDetails();
    details.skipProvisioning = skipProvisioning;
    onPremProvider.setDetails(details);
    onPremProvider.save();
    Universe.saveDetails(
        defaultUniverse.getUniverseUUID(),
        universe -> {
          UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
          UserIntent userIntent = universeDetails.getPrimaryCluster().userIntent;
          userIntent.provider = onPremProvider.getUuid().toString();
          userIntent.providerType = Common.CloudType.onprem;
          userIntent.regionList = ImmutableList.of(onPremRegion.getUuid());
        });
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
  }

  @Test
  public void testValidateParamsOnpremSkipProvisioningRejected() {
    setupOnPremUniverse(true /* skipProvisioning */);
    ProvisionUniverseNodesParams params = createTaskParams();
    assertThrows(PlatformServiceException.class, () -> submitTask(params));
  }

  @Test
  public void testValidateParamsOnpremNoSkipProvisioningAllowed() {
    setupOnPremUniverse(false /* skipProvisioning */);
    ProvisionUniverseNodesParams params = createTaskParams();
    TaskInfo taskInfo = submitTask(params);
  }

  @Test
  public void testValidateParamsAwsProviderAllowed() {
    ProvisionUniverseNodesParams params = createTaskParams();
    TaskInfo taskInfo = submitTask(params);
  }

  @Test
  public void testRunIneligibleNodeNameRejected() {
    ProvisionUniverseNodesParams params = createTaskParams();
    params.nodeNames = ImmutableSet.of("nonexistent-node");
    TaskInfo taskInfo = submitTask(params);
    assertEquals(TaskInfo.State.Failure, taskInfo.getTaskState());
  }

  @Test
  public void testRunSpecificNodeNameAccepted() {
    String firstNodeName = defaultUniverse.getNodes().iterator().next().getNodeName();
    ProvisionUniverseNodesParams params = createTaskParams();
    params.nodeNames = ImmutableSet.of(firstNodeName);
    TaskInfo taskInfo = submitTask(params);
  }

  @Test
  public void testRetryAfterAbortSucceeds() throws InterruptedException {
    Users defaultUser = ModelFactory.testUser(defaultCustomer);
    ProvisionUniverseNodesParams params = createTaskParams();
    params.expectedUniverseVersion = -1;
    params.creatingUser = defaultUser;
    params.sleepAfterMasterRestartMillis = 5;
    params.sleepAfterTServerRestartMillis = 5;
    params.skipNodeChecks = true;
    TestUtils.setFakeHttpContext(defaultUser);
    setPausePosition(2);
    UUID taskUUID = commissioner.submit(TaskType.ProvisionUniverseNodes, params);
    CustomerTask.create(
        defaultCustomer,
        defaultUniverse.getUniverseUUID(),
        taskUUID,
        CustomerTask.TargetType.Universe,
        CustomerTask.TaskType.ProvisionUniverseNodes,
        "fake-name");
    waitForTaskPaused(taskUUID);
    setAbortPosition(2);
    commissioner.resumeTask(taskUUID);
    TaskInfo taskInfo = waitForTask(taskUUID);
    assertEquals(TaskInfo.State.Aborted, taskInfo.getTaskState());
    clearAbortOrPausePositions();
    CustomerTask retryTask =
        customerTaskManager.retryCustomerTask(defaultCustomer.getUuid(), taskUUID);
    UUID retryTaskUUID = retryTask.getTaskUUID();
    assertNotEquals(taskUUID, retryTaskUUID);
    waitForTask(retryTaskUUID);
  }
}
