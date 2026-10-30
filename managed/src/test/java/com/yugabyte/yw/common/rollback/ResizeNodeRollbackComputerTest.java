// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.rollback;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.lenient;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.cloud.CloudAPI;
import com.yugabyte.yw.cloud.PublicCloudConstants;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.DeltaEvaluator;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.forms.ResizeNodeParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeOption;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.StateTransitionDetails;
import com.yugabyte.yw.models.helpers.TaskType;
import java.time.Duration;
import java.time.Instant;
import java.util.Date;
import java.util.HashMap;
import java.util.Map;
import java.util.Optional;
import java.util.UUID;
import org.apache.commons.lang3.time.DateUtils;
import org.junit.Before;
import org.junit.Test;
import play.libs.Json;

/**
 * Direct unit tests for {@link ResizeNodeRollbackComputer}. Focuses on the reverse-intent overlay
 * (never decrease volumeSize) and the cooldown gate (IOPS/throughput revert during window is
 * rejected; instance-only overlay during window is allowed). The full task path is exercised via
 * {@link com.yugabyte.yw.commissioner.tasks.upgrade.ResizeNodeTest} on the forward path; here we
 * only need to verify the computer's contract for {@code rollbackCustomerTask}.
 */
public class ResizeNodeRollbackComputerTest extends FakeDBApplication {

  private static final String DEFAULT_INSTANCE_TYPE = "c3.medium";
  private static final String NEW_INSTANCE_TYPE = "c4.medium";
  private static final int DEFAULT_VOLUME_SIZE = 100;
  private static final int NEW_VOLUME_SIZE = 200;
  private static final int MASTER_VOLUME_SIZE = 50;
  private static final int DEFAULT_DISK_IOPS = 3000;
  private static final int NEW_DISK_IOPS = 5000;
  private static final int DEFAULT_DISK_THROUGHPUT = 125;
  private static final int NEW_DISK_THROUGHPUT = 250;

  private Customer customer;
  private Universe universe;
  private ResizeNodeRollbackComputer computer;
  private CloudAPI cloudAPI;

  @Before
  public void setup() {
    customer = ModelFactory.testCustomer();
    universe = ModelFactory.createUniverse(customer.getId());
    // Give every node a well-defined deviceInfo with GP3 (has both IOPS and throughput
    // provisioning) so the computer sees a real volume shape rather than the ModelFactory
    // default.
    // ModelFactory.createUniverse leaves nodeDetailsSet empty when ybc is disabled and the aws
    // provider it creates has no region/AZ. Seed both: getProviderGetter reads the provider from
    // the AZ, and getNodesInCluster returns no nodes when the set is empty (cooldown gate no-ops).
    Provider awsProvider = Provider.get(customer.getUuid(), Common.CloudType.aws).get(0);
    Region region = Region.create(awsProvider, "region-1", "region-1", "yb-image-1");
    AvailabilityZone az =
        AvailabilityZone.createOrThrow(region, "az-1", "PlacementAZ 1", "subnet-1");
    UUID primaryClusterUuid = universe.getUniverseDetails().getPrimaryCluster().uuid;
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            u -> {
              UserIntent primary = u.getUniverseDetails().getPrimaryCluster().userIntent;
              primary.instanceType = DEFAULT_INSTANCE_TYPE;
              primary.providerType = Common.CloudType.aws;
              primary.deviceInfo = new DeviceInfo();
              primary.deviceInfo.numVolumes = 1;
              primary.deviceInfo.volumeSize = DEFAULT_VOLUME_SIZE;
              primary.deviceInfo.diskIops = DEFAULT_DISK_IOPS;
              primary.deviceInfo.throughput = DEFAULT_DISK_THROUGHPUT;
              primary.deviceInfo.storageType = PublicCloudConstants.StorageType.GP3;
              NodeDetails node = new NodeDetails();
              node.nodeIdx = 1;
              node.nodeName = "n1";
              node.nodeUuid = UUID.randomUUID();
              node.state = NodeDetails.NodeState.Live;
              node.placementUuid = primaryClusterUuid;
              node.azUuid = az.getUuid();
              node.isTserver = true;
              node.cloudInfo = new CloudSpecificInfo();
              node.cloudInfo.cloud = Common.CloudType.aws.toString();
              node.cloudInfo.instance_type = DEFAULT_INSTANCE_TYPE;
              node.cloudInfo.private_ip = "127.0.0.1";
              node.cloudInfo.region = az.getRegion().getCode();
              node.cloudInfo.az = az.getName();
              u.getUniverseDetails().nodeDetailsSet.add(node);
            });
    computer =
        app.injector()
            .instanceOf(com.google.inject.Injector.class)
            .getInstance(ResizeNodeRollbackComputer.class);
    cloudAPI = mock(CloudAPI.class);
    lenient().when(mockCloudAPIFactory.get(any())).thenReturn(cloudAPI);
    // Default: cloud still at the failed target IOPS, volume not grown, last modify outside the
    // AWS window. Volume defaults to before so fail-before-disk-update tests do not inherit a
    // cloud grow that never happened.
    stubCloudState(
        NEW_INSTANCE_TYPE,
        NEW_DISK_IOPS,
        NEW_DISK_THROUGHPUT,
        DEFAULT_VOLUME_SIZE,
        Instant.now().minus(Duration.ofHours(48)));
  }

  private RollbackContext buildContext(ResizeNodeParams failedParams, Date failedCreateTime) {
    JsonNode oldParams = Json.toJson(failedParams);
    TaskInfo taskInfo = mock(TaskInfo.class);
    when(taskInfo.getTaskType()).thenReturn(TaskType.ResizeNode);
    when(taskInfo.getUuid()).thenReturn(UUID.randomUUID());
    when(taskInfo.getCreateTime()).thenReturn(failedCreateTime);
    CustomerTask customerTask = mock(CustomerTask.class);
    when(customerTask.getTargetUUID()).thenReturn(universe.getUniverseUUID());
    return new RollbackContext(customer, customerTask, taskInfo, oldParams);
  }

  // Simulates the forward ResizeNode having captured a delta from before to target on freeze.
  // volumeSize grows, instanceType flips, IOPS/throughput bump - everything the reverse path
  // must consider.
  private void seedStateTransitionDetailsForResize(int targetVolumeSize) {
    UniverseDefinitionTaskParams before =
        Json.fromJson(
            Json.toJson(universe.getUniverseDetails()), UniverseDefinitionTaskParams.class);
    UniverseDefinitionTaskParams target =
        Json.fromJson(
            Json.toJson(universe.getUniverseDetails()), UniverseDefinitionTaskParams.class);
    UserIntent targetIntent = target.getPrimaryCluster().userIntent;
    targetIntent.instanceType = NEW_INSTANCE_TYPE;
    targetIntent.deviceInfo.volumeSize = targetVolumeSize;
    targetIntent.deviceInfo.diskIops = NEW_DISK_IOPS;
    targetIntent.deviceInfo.throughput = NEW_DISK_THROUGHPUT;
    JsonNode delta = DeltaEvaluator.buildDeltaJsonTree(before, target);
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            u -> u.setStateTransitionDetails(new StateTransitionDetails(true, delta)));
  }

  // Simulates a partially-persisted resize: cluster intent is at target on the universe.
  private void moveUniverseIntentToTarget(int volumeSize) {
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            u -> {
              UserIntent primary = u.getUniverseDetails().getPrimaryCluster().userIntent;
              primary.instanceType = NEW_INSTANCE_TYPE;
              primary.deviceInfo.volumeSize = volumeSize;
              primary.deviceInfo.diskIops = NEW_DISK_IOPS;
              primary.deviceInfo.throughput = NEW_DISK_THROUGHPUT;
            });
  }

  private ResizeNodeParams failedResizeParams(int targetVolumeSize) {
    ResizeNodeParams params = new ResizeNodeParams();
    params.setUniverseUUID(universe.getUniverseUUID());
    params.upgradeOption = UpgradeOption.ROLLING_UPGRADE;
    // Copy current clusters (post-forward-resize state) as the failed target snapshot.
    params.clusters =
        Json.fromJson(
                Json.toJson(universe.getUniverseDetails()), UniverseDefinitionTaskParams.class)
            .clusters;
    UserIntent target = params.getPrimaryCluster().userIntent;
    target.instanceType = NEW_INSTANCE_TYPE;
    target.deviceInfo.volumeSize = targetVolumeSize;
    target.deviceInfo.diskIops = NEW_DISK_IOPS;
    target.deviceInfo.throughput = NEW_DISK_THROUGHPUT;
    return params;
  }

  private void enableRollback() {
    mutableConfigFactory.globalRuntimeConf().setValue("yb.task.allow_resize_node_rollback", "true");
  }

  private void stubCloudDisk(Integer iops, Integer throughput, Instant lastModificationStart) {
    stubCloudState(NEW_INSTANCE_TYPE, iops, throughput, DEFAULT_VOLUME_SIZE, lastModificationStart);
  }

  private void stubCloudState(
      String instanceType,
      Integer iops,
      Integer throughput,
      Integer volumeSizeGb,
      Instant lastModificationStart) {
    lenient()
        .when(cloudAPI.describeNodeDataDiskSpec(any(), any()))
        .thenReturn(
            Optional.of(
                new CloudAPI.NodeDiskSpec(
                    instanceType, iops, throughput, volumeSizeGb, lastModificationStart)));
  }

  private void useAzurePremium() {
    Provider provider = Provider.get(customer.getUuid(), Common.CloudType.aws).get(0);
    provider.setCode(Common.CloudType.azu.toString());
    provider.save();
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            u ->
                u.getUniverseDetails().getPrimaryCluster().userIntent.deviceInfo.storageType =
                    PublicCloudConstants.StorageType.Premium_LRS);
  }

  private void useGcpHyperdisk() {
    Provider provider = Provider.get(customer.getUuid(), Common.CloudType.aws).get(0);
    provider.setCode(Common.CloudType.gcp.toString());
    provider.save();
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            u ->
                u.getUniverseDetails().getPrimaryCluster().userIntent.deviceInfo.storageType =
                    PublicCloudConstants.StorageType.Hyperdisk_Balanced);
  }

  @Test
  public void isEnabledFollowsRuntimeFlag() {
    assertFalse(computer.isEnabled());
    enableRollback();
    assertTrue(computer.isEnabled());
  }

  @Test
  public void computeRejectsWhenFlagOff() {
    seedStateTransitionDetailsForResize(NEW_VOLUME_SIZE);
    moveUniverseIntentToTarget(NEW_VOLUME_SIZE);
    ResizeNodeParams failedParams = failedResizeParams(NEW_VOLUME_SIZE);
    RollbackContext context = buildContext(failedParams, new Date());

    PlatformServiceException ex =
        assertThrows(PlatformServiceException.class, () -> computer.compute(context));
    assertTrue(ex.getMessage().contains("not enabled"));
  }

  @Test
  public void computeRejectsWhenStateTransitionDetailsMissing() {
    enableRollback();
    moveUniverseIntentToTarget(NEW_VOLUME_SIZE);
    ResizeNodeParams failedParams = failedResizeParams(NEW_VOLUME_SIZE);
    RollbackContext context = buildContext(failedParams, new Date());

    PlatformServiceException ex =
        assertThrows(PlatformServiceException.class, () -> computer.compute(context));
    assertTrue(ex.getMessage().contains("state_transition_details"));
  }

  @Test
  public void computeReversesInstanceAndKeepsVolumeSize() {
    enableRollback();
    seedStateTransitionDetailsForResize(NEW_VOLUME_SIZE);
    moveUniverseIntentToTarget(NEW_VOLUME_SIZE);
    // Cloud last-modify is stubbed 48h ago (setup), so the AWS window is expired even if YBA
    // timestamps are stale or missing.
    ResizeNodeParams failedParams = failedResizeParams(NEW_VOLUME_SIZE);
    Date twoDaysAgo = DateUtils.addHours(new Date(), -48);
    RollbackContext context = buildContext(failedParams, twoDaysAgo);

    RollbackSubmission submission = computer.compute(context);
    assertEquals(TaskType.RollbackResizeNode, submission.getRollbackTaskType());
    assertEquals(CustomerTask.TaskType.RollbackResizeNode, submission.getCustomerTaskType());
    assertFalse(submission.isSetPreviousTaskUUID());

    ResizeNodeParams rollbackParams = (ResizeNodeParams) submission.getParams();
    UserIntent restored = rollbackParams.getPrimaryCluster().userIntent;
    assertEquals(DEFAULT_INSTANCE_TYPE, restored.instanceType);
    // volumeSize must not decrease - keep the grown value.
    assertEquals(NEW_VOLUME_SIZE, restored.deviceInfo.volumeSize.intValue());
    // IOPS/throughput revert to before.
    assertEquals(DEFAULT_DISK_IOPS, restored.deviceInfo.diskIops.intValue());
    assertEquals(DEFAULT_DISK_THROUGHPUT, restored.deviceInfo.throughput.intValue());
    // expectedUniverseVersion is disabled so the resubmit does not race.
    assertEquals(Integer.valueOf(-1), rollbackParams.expectedUniverseVersion);
    // Azure cooldown clock for RollbackResizeNode precheck; same create time the submit gate used.
    assertEquals(twoDaysAgo, rollbackParams.getFailedTaskCreateTime());
  }

  @Test
  public void computeRestoresBeforeGFlags() {
    enableRollback();
    SpecificGFlags beforeFlags =
        SpecificGFlags.construct(Map.of("master-before", "1"), Map.of("tserver-before", "2"));
    Map<String, String> beforeMaster = new HashMap<>(Map.of("legacy-master", "a"));
    Map<String, String> beforeTserver = new HashMap<>(Map.of("legacy-tserver", "b"));
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            u -> {
              UserIntent primary = u.getUniverseDetails().getPrimaryCluster().userIntent;
              primary.specificGFlags = beforeFlags;
              primary.masterGFlags = new HashMap<>(beforeMaster);
              primary.tserverGFlags = new HashMap<>(beforeTserver);
            });
    seedStateTransitionDetailsForResize(DEFAULT_VOLUME_SIZE);
    ResizeNodeParams failedParams = failedResizeParams(DEFAULT_VOLUME_SIZE);
    failedParams.getPrimaryCluster().userIntent.specificGFlags =
        SpecificGFlags.construct(
            Map.of("master-before", "5", "added-master", "x"), Map.of("added-tserver", "y"));
    failedParams.masterGFlags = new HashMap<>(Map.of("legacy-master", "changed"));
    failedParams.tserverGFlags = new HashMap<>(Map.of("legacy-tserver", "changed"));
    RollbackContext context = buildContext(failedParams, DateUtils.addHours(new Date(), -48));

    RollbackSubmission submission = computer.compute(context);
    ResizeNodeParams rollbackParams = (ResizeNodeParams) submission.getParams();
    assertEquals(beforeFlags, rollbackParams.getPrimaryCluster().userIntent.specificGFlags);
    assertNotNull(rollbackParams.masterGFlags);
    assertNotNull(rollbackParams.tserverGFlags);
    assertEquals(beforeMaster, rollbackParams.masterGFlags);
    assertEquals(beforeTserver, rollbackParams.tserverGFlags);
  }

  @Test
  public void computeClearsForceResizeNodeFromFailedParams() {
    enableRollback();
    seedStateTransitionDetailsForResize(NEW_VOLUME_SIZE);
    moveUniverseIntentToTarget(NEW_VOLUME_SIZE);
    ResizeNodeParams failedParams = failedResizeParams(NEW_VOLUME_SIZE);
    failedParams.setForceResizeNode(true);
    RollbackContext context = buildContext(failedParams, DateUtils.addHours(new Date(), -48));

    RollbackSubmission submission = computer.compute(context);
    ResizeNodeParams rollbackParams = (ResizeNodeParams) submission.getParams();
    assertFalse(rollbackParams.isForceResizeNode());
  }

  @Test
  public void computeDoesNotGrowVolumeWhenFailBeforeDiskUpdate() {
    enableRollback();
    // Fail before Disk_Update / PersistResizeNode: universe intent is still 100, failed params
    // advertise 200. Rollback must not continue the grow.
    seedStateTransitionDetailsForResize(NEW_VOLUME_SIZE);
    ResizeNodeParams failedParams = failedResizeParams(NEW_VOLUME_SIZE);
    RollbackContext context = buildContext(failedParams, DateUtils.addHours(new Date(), -48));

    RollbackSubmission submission = computer.compute(context);
    ResizeNodeParams rollbackParams = (ResizeNodeParams) submission.getParams();
    UserIntent restored = rollbackParams.getPrimaryCluster().userIntent;
    assertEquals(DEFAULT_VOLUME_SIZE, restored.deviceInfo.volumeSize.intValue());
    assertEquals(DEFAULT_INSTANCE_TYPE, restored.instanceType);
  }

  @Test
  public void computeKeepsCurrentVolumeSizeWhenFailedTargetShrunkOnRetry() {
    enableRollback();
    // Before-freeze had 100. Current universe intent is 180 (partial persist). Failed-task
    // target is 150 and must not count: rollback keeps max(before=100, current=180) = 180.
    seedStateTransitionDetailsForResize(NEW_VOLUME_SIZE);
    moveUniverseIntentToTarget(180);
    ResizeNodeParams failedParams = failedResizeParams(150);
    Date twoDaysAgo = DateUtils.addHours(new Date(), -48);
    RollbackContext context = buildContext(failedParams, twoDaysAgo);

    RollbackSubmission submission = computer.compute(context);
    ResizeNodeParams rollbackParams = (ResizeNodeParams) submission.getParams();
    UserIntent restored = rollbackParams.getPrimaryCluster().userIntent;
    assertEquals(180, restored.deviceInfo.volumeSize.intValue());
  }

  @Test
  public void computeRejectsIopsRevertDuringAwsCooldown() {
    enableRollback();
    mutableConfigFactory.globalRuntimeConf().setValue("yb.aws.disk_resize_cooldown_hours", "6");
    seedStateTransitionDetailsForResize(NEW_VOLUME_SIZE);
    moveUniverseIntentToTarget(NEW_VOLUME_SIZE);
    // Cloud still at the failed IOPS; last modify 1h ago is inside the 6h window. YBA's
    // lastVolumeUpdateTime is ignored when AWS returned a startTime.
    stubCloudDisk(NEW_DISK_IOPS, NEW_DISK_THROUGHPUT, Instant.now().minus(Duration.ofHours(1)));
    Date oneHourAgo = DateUtils.addHours(new Date(), -1);
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            u ->
                u.getNodes()
                    .forEach(n -> n.lastVolumeUpdateTime = DateUtils.addHours(new Date(), -48)));
    ResizeNodeParams failedParams = failedResizeParams(NEW_VOLUME_SIZE);
    RollbackContext context = buildContext(failedParams, oneHourAgo);

    PlatformServiceException ex =
        assertThrows(PlatformServiceException.class, () -> computer.compute(context));
    assertTrue(ex.getMessage().contains("cooldown"));
  }

  @Test
  public void computeSkipsCooldownGateWhenSkipFlagEnabled() {
    // Same setup as computeRejectsIopsRevertDuringAwsCooldown - would normally throw. The skip
    // flag bypasses the local pre-check and lets the submission through; the cloud is still the
    // final gate.
    enableRollback();
    mutableConfigFactory.globalRuntimeConf().setValue("yb.aws.disk_resize_cooldown_hours", "6");
    mutableConfigFactory
        .globalRuntimeConf()
        .setValue("yb.task.skip_resize_node_rollback_cooldown", "true");
    seedStateTransitionDetailsForResize(NEW_VOLUME_SIZE);
    moveUniverseIntentToTarget(NEW_VOLUME_SIZE);
    stubCloudDisk(NEW_DISK_IOPS, NEW_DISK_THROUGHPUT, Instant.now().minus(Duration.ofHours(1)));
    Date oneHourAgo = DateUtils.addHours(new Date(), -1);
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            u -> u.getNodes().forEach(n -> n.lastVolumeUpdateTime = oneHourAgo));
    ResizeNodeParams failedParams = failedResizeParams(NEW_VOLUME_SIZE);
    RollbackContext context = buildContext(failedParams, oneHourAgo);

    RollbackSubmission submission = computer.compute(context);
    assertNotNull(submission);
    assertEquals(TaskType.RollbackResizeNode, submission.getRollbackTaskType());
  }

  @Test
  public void computeAllowsInstanceOnlyRevertDuringAwsCooldown() {
    enableRollback();
    mutableConfigFactory.globalRuntimeConf().setValue("yb.aws.disk_resize_cooldown_hours", "6");
    // Delta only flips instance type (no IOPS/throughput bump), so the reverse path emits no
    // Disk_Update. Cooldown must not gate an instance-only rollback.
    UniverseDefinitionTaskParams before =
        Json.fromJson(
            Json.toJson(universe.getUniverseDetails()), UniverseDefinitionTaskParams.class);
    UniverseDefinitionTaskParams target =
        Json.fromJson(
            Json.toJson(universe.getUniverseDetails()), UniverseDefinitionTaskParams.class);
    target.getPrimaryCluster().userIntent.instanceType = NEW_INSTANCE_TYPE;
    JsonNode delta = DeltaEvaluator.buildDeltaJsonTree(before, target);
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            u -> u.setStateTransitionDetails(new StateTransitionDetails(true, delta)));
    // Advance current universe to the instance-only target.
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            u ->
                u.getUniverseDetails().getPrimaryCluster().userIntent.instanceType =
                    NEW_INSTANCE_TYPE);
    Date oneHourAgo = DateUtils.addHours(new Date(), -1);
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            u -> u.getNodes().forEach(n -> n.lastVolumeUpdateTime = oneHourAgo));
    // Cloud already at desired IOPS/throughput so always-describe does not invent a disk modify.
    stubCloudState(
        NEW_INSTANCE_TYPE,
        DEFAULT_DISK_IOPS,
        DEFAULT_DISK_THROUGHPUT,
        DEFAULT_VOLUME_SIZE,
        Instant.now().minus(Duration.ofHours(1)));
    ResizeNodeParams failedParams = failedResizeParams(DEFAULT_VOLUME_SIZE);
    // Keep IOPS/throughput at before so the reverse would not touch them.
    UserIntent target2 = failedParams.getPrimaryCluster().userIntent;
    target2.deviceInfo.diskIops = DEFAULT_DISK_IOPS;
    target2.deviceInfo.throughput = DEFAULT_DISK_THROUGHPUT;
    target2.deviceInfo.volumeSize = DEFAULT_VOLUME_SIZE;
    RollbackContext context = buildContext(failedParams, oneHourAgo);

    RollbackSubmission submission = computer.compute(context);
    assertNotNull(submission);
    UserIntent restored =
        ((ResizeNodeParams) submission.getParams()).getPrimaryCluster().userIntent;
    assertEquals(DEFAULT_INSTANCE_TYPE, restored.instanceType);
  }

  @Test
  public void computeRejectsWhenCloudLastModifyInsideAwsWindowAndLastVolumeUpdateTimeIsNull() {
    enableRollback();
    mutableConfigFactory.globalRuntimeConf().setValue("yb.aws.disk_resize_cooldown_hours", "6");
    seedStateTransitionDetailsForResize(NEW_VOLUME_SIZE);
    moveUniverseIntentToTarget(NEW_VOLUME_SIZE);
    // Persist never wrote lastVolumeUpdateTime. AWS still has a modify from 1h ago, so YBA
    // timestamps are not the clock.
    stubCloudDisk(NEW_DISK_IOPS, NEW_DISK_THROUGHPUT, Instant.now().minus(Duration.ofHours(1)));
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            u -> u.getNodes().forEach(n -> n.lastVolumeUpdateTime = null));
    Date oneHourAgo = DateUtils.addHours(new Date(), -1);
    ResizeNodeParams failedParams = failedResizeParams(NEW_VOLUME_SIZE);
    RollbackContext context = buildContext(failedParams, oneHourAgo);

    PlatformServiceException ex =
        assertThrows(PlatformServiceException.class, () -> computer.compute(context));
    assertTrue(ex.getMessage().contains("cooldown"));
  }

  @Test
  public void wouldChangeIopsOrThroughputHandlesNullsAndEqualValues() {
    DeviceInfo a = new DeviceInfo();
    a.diskIops = 3000;
    a.throughput = 125;
    DeviceInfo sameAsA = a.clone();
    assertFalse(ResizeNodeRollbackComputer.wouldChangeIopsOrThroughput(a, sameAsA));
    DeviceInfo iopsChange = a.clone();
    iopsChange.diskIops = 5000;
    assertTrue(ResizeNodeRollbackComputer.wouldChangeIopsOrThroughput(a, iopsChange));
    assertFalse(ResizeNodeRollbackComputer.wouldChangeIopsOrThroughput(null, a));
    assertFalse(ResizeNodeRollbackComputer.wouldChangeIopsOrThroughput(a, null));
  }

  @Test
  public void computeDropsRuntimeInfoInheritance() {
    enableRollback();
    seedStateTransitionDetailsForResize(NEW_VOLUME_SIZE);
    moveUniverseIntentToTarget(NEW_VOLUME_SIZE);
    ResizeNodeParams failedParams = failedResizeParams(NEW_VOLUME_SIZE);
    failedParams.setPreviousTaskUUID(UUID.randomUUID());
    Date twoDaysAgo = DateUtils.addHours(new Date(), -48);
    RollbackContext context = buildContext(failedParams, twoDaysAgo);

    RollbackSubmission submission = computer.compute(context);
    // Fresh rollback: CustomerTaskManager clears previousTaskUUID on the submitted params
    // whenever setPreviousTaskUUID=false; assert the computer signals that.
    assertFalse(submission.isSetPreviousTaskUUID());
    assertEquals(
        Integer.valueOf(-1), ((ResizeNodeParams) submission.getParams()).expectedUniverseVersion);
  }

  @Test
  public void computeRefusesKubernetesUniverse() {
    enableRollback();
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            u ->
                u.getUniverseDetails().getPrimaryCluster().userIntent.providerType =
                    Common.CloudType.kubernetes);
    seedStateTransitionDetailsForResize(NEW_VOLUME_SIZE);
    ResizeNodeParams failedParams = failedResizeParams(NEW_VOLUME_SIZE);
    RollbackContext context = buildContext(failedParams, DateUtils.addHours(new Date(), -48));

    PlatformServiceException ex =
        assertThrows(PlatformServiceException.class, () -> computer.compute(context));
    assertTrue(ex.getMessage().contains("Kubernetes"));
  }

  @Test
  public void computeRejectsWhenAwsModificationTimeMissing() {
    enableRollback();
    mutableConfigFactory.globalRuntimeConf().setValue("yb.aws.disk_resize_cooldown_hours", "6");
    seedStateTransitionDetailsForResize(NEW_VOLUME_SIZE);
    moveUniverseIntentToTarget(NEW_VOLUME_SIZE);
    stubCloudDisk(NEW_DISK_IOPS, NEW_DISK_THROUGHPUT, null);
    ResizeNodeParams failedParams = failedResizeParams(NEW_VOLUME_SIZE);
    RollbackContext context = buildContext(failedParams, DateUtils.addHours(new Date(), -48));

    PlatformServiceException ex =
        assertThrows(PlatformServiceException.class, () -> computer.compute(context));
    assertTrue(ex.getMessage().contains("Could not verify cloud disk cooldown"));
  }

  @Test
  public void computeRefusesKubernetesInMultiCloudProviderSpecifications() {
    enableRollback();
    Provider k8sProvider = ModelFactory.kubernetesProvider(customer);
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            u -> {
              UserIntent intent = u.getUniverseDetails().getPrimaryCluster().userIntent;
              UUID providerUUID = intent.maybeGetSingleProviderUUID().get();
              // Read before the first spec exists; afterwards lookups resolve from the spec.
              Common.CloudType providerType = intent.getProviderType(providerUUID);
              TestUtils.specificationProviderInitializer(intent, providerUUID)
                  .setProviderType(providerType)
                  .setAccessCode(intent.accessKeyCode)
                  .setInstanceTags(intent.instanceTags)
                  .setDeviceInfo(intent.deviceInfo)
                  .setInstanceType(intent.instanceType);
              TestUtils.specificationProviderInitializer(intent, k8sProvider.getUuid())
                  .setProviderType(Common.CloudType.kubernetes)
                  .setInstanceType(intent.instanceType)
                  .setDeviceInfo(intent.deviceInfo.clone());
            });
    seedStateTransitionDetailsForResize(NEW_VOLUME_SIZE);
    ResizeNodeParams failedParams = failedResizeParams(NEW_VOLUME_SIZE);
    RollbackContext context = buildContext(failedParams, DateUtils.addHours(new Date(), -48));

    PlatformServiceException ex =
        assertThrows(PlatformServiceException.class, () -> computer.compute(context));
    assertTrue(ex.getMessage().contains("Kubernetes"));
  }

  @Test
  public void computeRejectsWhenRollbackCheckpointCrossed() {
    enableRollback();
    seedStateTransitionDetailsForResize(NEW_VOLUME_SIZE);
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            u -> {
              StateTransitionDetails details = u.getStateTransitionDetails();
              details.setRollbackSafe(false);
              u.setStateTransitionDetails(details);
            });
    moveUniverseIntentToTarget(NEW_VOLUME_SIZE);
    ResizeNodeParams failedParams = failedResizeParams(NEW_VOLUME_SIZE);
    RollbackContext context = buildContext(failedParams, DateUtils.addHours(new Date(), -48));

    PlatformServiceException ex =
        assertThrows(PlatformServiceException.class, () -> computer.compute(context));
    assertTrue(ex.getMessage().contains("rollback checkpoint was crossed"));
  }

  @Test
  public void computeAllowsWhenCloudAlreadyAtDesiredIops() {
    enableRollback();
    mutableConfigFactory.globalRuntimeConf().setValue("yb.aws.disk_resize_cooldown_hours", "6");
    seedStateTransitionDetailsForResize(NEW_VOLUME_SIZE);
    moveUniverseIntentToTarget(NEW_VOLUME_SIZE);
    // YBA still at the failed target and lastVolumeUpdateTime is inside the window, but the cloud
    // already matches the rollback intent so a reverse Disk_Update would no-op.
    stubCloudDisk(
        DEFAULT_DISK_IOPS, DEFAULT_DISK_THROUGHPUT, Instant.now().minus(Duration.ofHours(1)));
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            u ->
                u.getNodes()
                    .forEach(n -> n.lastVolumeUpdateTime = DateUtils.addHours(new Date(), -1)));
    ResizeNodeParams failedParams = failedResizeParams(NEW_VOLUME_SIZE);
    RollbackContext context = buildContext(failedParams, DateUtils.addHours(new Date(), -1));

    RollbackSubmission submission = computer.compute(context);
    assertNotNull(submission);
    UserIntent restored =
        ((ResizeNodeParams) submission.getParams()).getPrimaryCluster().userIntent;
    assertEquals(DEFAULT_DISK_IOPS, restored.deviceInfo.diskIops.intValue());
  }

  @Test
  public void computeAllowsWhenCloudLastModifyOutsideAwsWindow() {
    enableRollback();
    mutableConfigFactory.globalRuntimeConf().setValue("yb.aws.disk_resize_cooldown_hours", "6");
    seedStateTransitionDetailsForResize(NEW_VOLUME_SIZE);
    moveUniverseIntentToTarget(NEW_VOLUME_SIZE);
    stubCloudDisk(NEW_DISK_IOPS, NEW_DISK_THROUGHPUT, Instant.now().minus(Duration.ofHours(48)));
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            u ->
                u.getNodes()
                    .forEach(n -> n.lastVolumeUpdateTime = DateUtils.addHours(new Date(), -1)));
    ResizeNodeParams failedParams = failedResizeParams(NEW_VOLUME_SIZE);
    RollbackContext context = buildContext(failedParams, DateUtils.addHours(new Date(), -1));

    RollbackSubmission submission = computer.compute(context);
    assertNotNull(submission);
  }

  @Test
  public void computeAllowsAwsWhenCloudHasNoModificationHistory() {
    enableRollback();
    mutableConfigFactory.globalRuntimeConf().setValue("yb.aws.disk_resize_cooldown_hours", "6");
    seedStateTransitionDetailsForResize(NEW_VOLUME_SIZE);
    moveUniverseIntentToTarget(NEW_VOLUME_SIZE);
    stubCloudDisk(NEW_DISK_IOPS, NEW_DISK_THROUGHPUT, Instant.EPOCH);
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            u ->
                u.getNodes()
                    .forEach(n -> n.lastVolumeUpdateTime = DateUtils.addHours(new Date(), -1)));
    ResizeNodeParams failedParams = failedResizeParams(NEW_VOLUME_SIZE);
    RollbackContext context = buildContext(failedParams, DateUtils.addHours(new Date(), -1));

    RollbackSubmission submission = computer.compute(context);
    assertNotNull(submission);
  }

  @Test
  public void computeRejectsWhenDescribeThrows() {
    enableRollback();
    mutableConfigFactory.globalRuntimeConf().setValue("yb.aws.disk_resize_cooldown_hours", "6");
    seedStateTransitionDetailsForResize(NEW_VOLUME_SIZE);
    moveUniverseIntentToTarget(NEW_VOLUME_SIZE);
    when(cloudAPI.describeNodeDataDiskSpec(any(), any()))
        .thenThrow(new RuntimeException("ec2 unavailable"));
    ResizeNodeParams failedParams = failedResizeParams(NEW_VOLUME_SIZE);
    RollbackContext context = buildContext(failedParams, DateUtils.addHours(new Date(), -48));

    PlatformServiceException ex =
        assertThrows(PlatformServiceException.class, () -> computer.compute(context));
    assertTrue(ex.getMessage().contains("Could not verify cloud disk cooldown"));
    assertFalse(ex.getMessage().contains("no cooldown"));
  }

  @Test
  public void computeRejectsWhenGcpHyperdiskHasNoModificationTime() {
    enableRollback();
    useGcpHyperdisk();
    seedStateTransitionDetailsForResize(NEW_VOLUME_SIZE);
    moveUniverseIntentToTarget(NEW_VOLUME_SIZE);
    stubCloudDisk(NEW_DISK_IOPS, NEW_DISK_THROUGHPUT, null);
    ResizeNodeParams failedParams = failedResizeParams(NEW_VOLUME_SIZE);
    RollbackContext context = buildContext(failedParams, DateUtils.addHours(new Date(), -48));

    PlatformServiceException ex =
        assertThrows(PlatformServiceException.class, () -> computer.compute(context));
    assertTrue(ex.getMessage().contains("Could not verify cloud disk cooldown"));
  }

  @Test
  public void computeAllowsAzureWhenCloudAlreadyAtDesired() {
    enableRollback();
    useAzurePremium();
    seedStateTransitionDetailsForResize(NEW_VOLUME_SIZE);
    moveUniverseIntentToTarget(NEW_VOLUME_SIZE);
    stubCloudDisk(DEFAULT_DISK_IOPS, DEFAULT_DISK_THROUGHPUT, null);
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            u ->
                u.getNodes()
                    .forEach(n -> n.lastVolumeUpdateTime = DateUtils.addHours(new Date(), -1)));
    ResizeNodeParams failedParams = failedResizeParams(NEW_VOLUME_SIZE);
    RollbackContext context = buildContext(failedParams, DateUtils.addHours(new Date(), -1));

    RollbackSubmission submission = computer.compute(context);
    assertNotNull(submission);
  }

  @Test
  public void computeRejectsAzureWhenFailedTaskCreateInsideWindow() {
    enableRollback();
    useAzurePremium();
    seedStateTransitionDetailsForResize(NEW_VOLUME_SIZE);
    moveUniverseIntentToTarget(NEW_VOLUME_SIZE);
    stubCloudDisk(NEW_DISK_IOPS, NEW_DISK_THROUGHPUT, null);
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            u -> u.getNodes().forEach(n -> n.lastVolumeUpdateTime = null));
    ResizeNodeParams failedParams = failedResizeParams(NEW_VOLUME_SIZE);
    RollbackContext context = buildContext(failedParams, DateUtils.addHours(new Date(), -1));

    PlatformServiceException ex =
        assertThrows(PlatformServiceException.class, () -> computer.compute(context));
    assertTrue(ex.getMessage().contains("cooldown"));
    assertTrue(ex.getMessage().contains("azu"));
  }

  @Test
  public void computeAllowsAzureWhenBothClocksOutsideWindow() {
    enableRollback();
    useAzurePremium();
    seedStateTransitionDetailsForResize(NEW_VOLUME_SIZE);
    moveUniverseIntentToTarget(NEW_VOLUME_SIZE);
    stubCloudDisk(NEW_DISK_IOPS, NEW_DISK_THROUGHPUT, null);
    Date twoDaysAgo = DateUtils.addHours(new Date(), -48);
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            u -> u.getNodes().forEach(n -> n.lastVolumeUpdateTime = twoDaysAgo));
    ResizeNodeParams failedParams = failedResizeParams(NEW_VOLUME_SIZE);
    RollbackContext context = buildContext(failedParams, twoDaysAgo);

    RollbackSubmission submission = computer.compute(context);
    assertNotNull(submission);
  }

  @Test
  public void computeRejectsAzureWhenBothClocksMissing() {
    enableRollback();
    useAzurePremium();
    seedStateTransitionDetailsForResize(NEW_VOLUME_SIZE);
    moveUniverseIntentToTarget(NEW_VOLUME_SIZE);
    stubCloudDisk(NEW_DISK_IOPS, NEW_DISK_THROUGHPUT, null);
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            u -> u.getNodes().forEach(n -> n.lastVolumeUpdateTime = null));
    ResizeNodeParams failedParams = failedResizeParams(NEW_VOLUME_SIZE);
    RollbackContext context = buildContext(failedParams, null);

    PlatformServiceException ex =
        assertThrows(PlatformServiceException.class, () -> computer.compute(context));
    assertTrue(ex.getMessage().contains("Could not verify cloud disk cooldown"));
  }

  @Test
  public void computeRejectsCooldownWhenYbaAlreadyAtBeforeButCloudStillAtNew() {
    // Persist aborted after Disk_Update: YBA IOPS already look like before, cloud still at the
    // failed target. Cooldown must still reject.
    enableRollback();
    mutableConfigFactory.globalRuntimeConf().setValue("yb.aws.disk_resize_cooldown_hours", "6");
    seedStateTransitionDetailsForResize(NEW_VOLUME_SIZE);
    // Universe intent never moved to the failed target (persist never ran).
    stubCloudDisk(NEW_DISK_IOPS, NEW_DISK_THROUGHPUT, Instant.now().minus(Duration.ofHours(1)));
    ResizeNodeParams failedParams = failedResizeParams(NEW_VOLUME_SIZE);
    RollbackContext context = buildContext(failedParams, DateUtils.addHours(new Date(), -1));

    PlatformServiceException ex =
        assertThrows(PlatformServiceException.class, () -> computer.compute(context));
    assertTrue(ex.getMessage().contains("cooldown"));
  }

  @Test
  public void computeAllowsWhenYbaAndCloudAlreadyAtBefore() {
    enableRollback();
    mutableConfigFactory.globalRuntimeConf().setValue("yb.aws.disk_resize_cooldown_hours", "6");
    seedStateTransitionDetailsForResize(NEW_VOLUME_SIZE);
    stubCloudState(
        DEFAULT_INSTANCE_TYPE,
        DEFAULT_DISK_IOPS,
        DEFAULT_DISK_THROUGHPUT,
        DEFAULT_VOLUME_SIZE,
        Instant.now().minus(Duration.ofHours(1)));
    ResizeNodeParams failedParams = failedResizeParams(NEW_VOLUME_SIZE);
    RollbackContext context = buildContext(failedParams, DateUtils.addHours(new Date(), -1));

    RollbackSubmission submission = computer.compute(context);
    assertNotNull(submission);
  }

  @Test
  public void computeKeepsCloudVolumeSizeWhenYbaNeverPersistedGrow() {
    enableRollback();
    seedStateTransitionDetailsForResize(NEW_VOLUME_SIZE);
    // YBA still at 100; cloud already grew to 200 before PersistResizeNode aborted.
    stubCloudState(
        NEW_INSTANCE_TYPE,
        NEW_DISK_IOPS,
        NEW_DISK_THROUGHPUT,
        NEW_VOLUME_SIZE,
        Instant.now().minus(Duration.ofHours(48)));
    ResizeNodeParams failedParams = failedResizeParams(NEW_VOLUME_SIZE);
    RollbackContext context = buildContext(failedParams, DateUtils.addHours(new Date(), -48));

    RollbackSubmission submission = computer.compute(context);
    UserIntent restored =
        ((ResizeNodeParams) submission.getParams()).getPrimaryCluster().userIntent;
    assertEquals(NEW_VOLUME_SIZE, restored.deviceInfo.volumeSize.intValue());
  }

  @Test
  public void computeDoesNotInflateMasterVolumeFromTserverCloudGrow() {
    enableRollback();
    final String masterNodeName = "n-master";
    UUID azUuid = universe.getNodes().stream().findFirst().orElseThrow().azUuid;
    AvailabilityZone az = AvailabilityZone.getOrBadRequest(azUuid);
    UUID primaryClusterUuid = universe.getUniverseDetails().getPrimaryCluster().uuid;
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            u -> {
              UserIntent primary = u.getUniverseDetails().getPrimaryCluster().userIntent;
              primary.dedicatedNodes = true;
              primary.masterInstanceType = DEFAULT_INSTANCE_TYPE;
              primary.masterDeviceInfo = primary.deviceInfo.clone();
              primary.masterDeviceInfo.volumeSize = MASTER_VOLUME_SIZE;
              NodeDetails tserver = u.getNode("n1");
              tserver.isMaster = false;
              tserver.isTserver = true;
              tserver.dedicatedTo = ServerType.TSERVER;
              NodeDetails master = new NodeDetails();
              master.nodeIdx = 2;
              master.nodeName = masterNodeName;
              master.nodeUuid = UUID.randomUUID();
              master.state = NodeDetails.NodeState.Live;
              master.placementUuid = primaryClusterUuid;
              master.azUuid = az.getUuid();
              master.isMaster = true;
              master.isTserver = false;
              master.dedicatedTo = ServerType.MASTER;
              master.cloudInfo = new CloudSpecificInfo();
              master.cloudInfo.cloud = Common.CloudType.aws.toString();
              master.cloudInfo.instance_type = DEFAULT_INSTANCE_TYPE;
              master.cloudInfo.private_ip = "127.0.0.2";
              master.cloudInfo.region = az.getRegion().getCode();
              master.cloudInfo.az = az.getName();
              u.getUniverseDetails().nodeDetailsSet.add(master);
            });
    seedStateTransitionDetailsForResize(NEW_VOLUME_SIZE);
    // Tserver cloud grew to 200 before persist; dedicated master cloud stayed at 50.
    Instant expired = Instant.now().minus(Duration.ofHours(48));
    when(cloudAPI.describeNodeDataDiskSpec(any(), any()))
        .thenAnswer(
            invocation -> {
              NodeDetails node = invocation.getArgument(1);
              int volume =
                  masterNodeName.equals(node.nodeName) ? MASTER_VOLUME_SIZE : NEW_VOLUME_SIZE;
              return Optional.of(
                  new CloudAPI.NodeDiskSpec(
                      DEFAULT_INSTANCE_TYPE,
                      DEFAULT_DISK_IOPS,
                      DEFAULT_DISK_THROUGHPUT,
                      volume,
                      expired));
            });
    ResizeNodeParams failedParams = failedResizeParams(NEW_VOLUME_SIZE);
    RollbackContext context = buildContext(failedParams, DateUtils.addHours(new Date(), -48));

    RollbackSubmission submission = computer.compute(context);
    UserIntent restored =
        ((ResizeNodeParams) submission.getParams()).getPrimaryCluster().userIntent;
    assertEquals(NEW_VOLUME_SIZE, restored.deviceInfo.volumeSize.intValue());
    assertNotNull(restored.masterDeviceInfo);
    assertEquals(MASTER_VOLUME_SIZE, restored.masterDeviceInfo.volumeSize.intValue());
  }

  /** Simple identity check: {@link Cluster} lookup by uuid should match primary. */
  @Test
  public void primaryClusterLookupSanity() {
    Cluster primary = universe.getUniverseDetails().getPrimaryCluster();
    assertEquals(primary, universe.getUniverseDetails().getClusterByUuid(primary.uuid));
  }
}
