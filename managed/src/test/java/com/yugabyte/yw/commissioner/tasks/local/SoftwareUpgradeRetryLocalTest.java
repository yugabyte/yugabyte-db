// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.local;

import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.ReleaseManager;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.forms.AZUpgradeStep;
import com.yugabyte.yw.forms.CanaryPauseState;
import com.yugabyte.yw.forms.CanaryUpgradeConfig;
import com.yugabyte.yw.forms.FinalizeUpgradeParams;
import com.yugabyte.yw.forms.RollbackUpgradeParams;
import com.yugabyte.yw.forms.SoftwareUpgradeParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.SoftwareUpgradeState;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.ProviderDetails;
import com.yugabyte.yw.models.ScopedRuntimeConfig;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.YugawareProperty;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.models.helpers.provider.LocalCloudInfo;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.TreeMap;
import java.util.UUID;
import java.util.function.Supplier;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.junit.Before;
import org.junit.Test;

/**
 * Local-provider fault-injection tests for {@link
 * com.yugabyte.yw.commissioner.tasks.upgrade.SoftwareUpgradeYB} and related upgrade flows.
 *
 * <p>At each subtask position the task is aborted, retried, and the abort position is advanced
 * until the upgrade fully succeeds against real loopback yb-master/yb-tserver processes.
 */
@Slf4j
public class SoftwareUpgradeRetryLocalTest extends LocalProviderUniverseTestBase {

  public static final String OLD_VERSION_WITH_ROLLBACK = "2.21.0.0-b340";
  private static final String OLD_VERSION_WITH_ROLLBACK_URL =
      "https://s3.us-west-2.amazonaws.com/uploads.dev.yugabyte.com/"
          + "local-provider-test/2.21.0.0-b340/yugabyte-2.21.0.0-b340-%s-%s.tar.gz";

  private static final String PG_11_DB_VERSION = "2024.2.3.0-b116";
  private static final String PG_11_DB_VERSION_URL =
      "https://software.yugabyte.com/releases/2024.2.3.0/yugabyte-2024.2.3.0-b116-%s-%s.tar.gz";

  private static final String PG_15_DB_VERSION = "2025.1.1.0-b73";
  private static final String PG_15_DB_VERSION_URL =
      "https://s3.us-west-2.amazonaws.com/uploads.dev.yugabyte.com/"
          + "local-provider-test/2025.1.1.0-b73/yugabyte-2025.1.1.0-b73-%s-%s.tar.gz";

  private static final long WAIT_SLEEP_MS = 500;
  private static final int WAIT_MAX_RETRIES = 3600;

  @Override
  protected int getTestTimeoutSeconds() {
    return 24000;
  }

  private static final class SubtaskCapture {
    final int totalSubTaskCount;
    final int freezeIdx;

    SubtaskCapture(int totalSubTaskCount, int freezeIdx) {
      this.totalSubTaskCount = totalSubTaskCount;
      this.freezeIdx = freezeIdx;
    }

    int getFirstAbortPosition() {
      return freezeIdx >= 0 ? freezeIdx + 1 : 0;
    }
  }

  private static final class PauseCheckpoint {
    final int position;
    final CanaryPauseState canaryPauseState;

    PauseCheckpoint(int position, CanaryPauseState canaryPauseState) {
      this.position = position;
      this.canaryPauseState = canaryPauseState;
    }

    boolean isPresent() {
      return position >= 0;
    }
  }

  @Before
  public void setup() {
    addRelease(OLD_VERSION_WITH_ROLLBACK, OLD_VERSION_WITH_ROLLBACK_URL);
    addRelease(PG_11_DB_VERSION, PG_11_DB_VERSION_URL);
    addRelease(PG_15_DB_VERSION, PG_15_DB_VERSION_URL);
    localNodeManager.addVersionBinPath(
        OLD_VERSION_WITH_ROLLBACK,
        baseDir + "/yugabyte/yugabyte-" + OLD_VERSION_WITH_ROLLBACK + "/bin");
    localNodeManager.addVersionBinPath(
        PG_11_DB_VERSION, baseDir + "/yugabyte/yugabyte-" + PG_11_DB_VERSION + "/bin");
    localNodeManager.addVersionBinPath(
        PG_15_DB_VERSION, baseDir + "/yugabyte/yugabyte-" + PG_15_DB_VERSION + "/bin");

    runtimeConfService.setKey(
        customer.getUuid(),
        ScopedRuntimeConfig.GLOBAL_SCOPE_UUID,
        GlobalConfKeys.skipVersionChecks.getKey(),
        "true",
        true);
  }

  @Test
  public void testSoftwareUpgradeWithRetries() throws InterruptedException {
    Universe universe = createRollbackCapableUniverse();
    final UUID universeUUID = universe.getUniverseUUID();
    SoftwareUpgradeParams params = getBaseUpgradeParams();
    params.setUniverseUUID(universeUUID);
    params.ybSoftwareVersion = PG_11_DB_VERSION;

    runFaultInjectedUpgrade(
        () ->
            upgradeUniverseHandler.upgradeDBVersion(
                params, customer, Universe.getOrBadRequest(universeUUID)),
        () -> {
          Universe upgraded = Universe.getOrBadRequest(universeUUID);
          assertEquals(
              PG_11_DB_VERSION,
              upgraded.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion);
          assertTrue(upgraded.getUniverseDetails().isSoftwareRollbackAllowed);
          verifyPayload();
          verifyYSQL(upgraded);
        });
  }

  @Test
  public void testPG15SoftwareUpgradeWithRetries() throws InterruptedException {
    Universe universe = createPg11Universe();
    final UUID universeUUID = universe.getUniverseUUID();
    SoftwareUpgradeParams params = getBaseUpgradeParams();
    params.setUniverseUUID(universeUUID);
    params.ybSoftwareVersion = PG_15_DB_VERSION;

    runFaultInjectedUpgrade(
        () ->
            upgradeUniverseHandler.upgradeDBVersion(
                params, customer, Universe.getOrBadRequest(universeUUID)),
        () -> {
          Universe upgraded = Universe.getOrBadRequest(universeUUID);
          assertEquals(
              PG_15_DB_VERSION,
              upgraded.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion);
          assertTrue(upgraded.getUniverseDetails().isSoftwareRollbackAllowed);
          verifyPayload();
          verifyYSQL(upgraded);
        });
  }

  @Test
  public void testRollbackUpgradeWithRetries() throws InterruptedException {
    Universe universe = createRollbackCapableUniverse();
    runUpgradeThenFaultInjectedRollback(
        universe.getUniverseUUID(), PG_11_DB_VERSION, OLD_VERSION_WITH_ROLLBACK);
  }

  @Test
  public void testPG15RollbackUpgradeWithRetries() throws InterruptedException {
    Universe universe = createPg11Universe();

    runUpgradeThenFaultInjectedRollback(
        universe.getUniverseUUID(), PG_15_DB_VERSION, PG_11_DB_VERSION);
  }

  @Test
  public void testFinalizeUpgradeWithRetries() throws InterruptedException {
    Universe universe = createRollbackCapableUniverse();
    final UUID universeUUID = universe.getUniverseUUID();
    SoftwareUpgradeParams upgradeParams = getBaseUpgradeParams();
    upgradeParams.setUniverseUUID(universeUUID);
    upgradeParams.ybSoftwareVersion = PG_11_DB_VERSION;
    TaskInfo upgradeTask =
        waitForTask(
            upgradeUniverseHandler.upgradeDBVersion(
                upgradeParams, customer, Universe.getOrBadRequest(universeUUID)),
            WAIT_SLEEP_MS,
            WAIT_MAX_RETRIES);
    assertEquals(Success, upgradeTask.getTaskState());
    assertTrue(
        Universe.getOrBadRequest(universeUUID).getUniverseDetails().isSoftwareRollbackAllowed);

    FinalizeUpgradeParams finalizeParams = new FinalizeUpgradeParams();
    finalizeParams.setUniverseUUID(universeUUID);
    finalizeParams.expectedUniverseVersion = -1;

    runFaultInjectedUpgrade(
        () ->
            upgradeUniverseHandler.finalizeUpgrade(
                finalizeParams, customer, Universe.getOrBadRequest(universeUUID)),
        () -> {
          Universe finalized = Universe.getOrBadRequest(universeUUID);
          assertEquals(
              SoftwareUpgradeState.Ready, finalized.getUniverseDetails().softwareUpgradeState);
          assertFalse(finalized.getUniverseDetails().isSoftwareRollbackAllowed);
          verifyPayload();
          verifyYSQL(finalized);
        });
  }

  @Test
  public void testCanarySoftwareUpgradeFaultInjectedWithPauses() throws InterruptedException {
    Universe universe = createRollbackCapableUniverse();
    final UUID universeUUID = universe.getUniverseUUID();
    SoftwareUpgradeParams params = buildCanaryUpgradeParams(universe, PG_11_DB_VERSION);
    runCanaryFaultInjectedUpgradeWithPauses(
        universeUUID,
        () ->
            upgradeUniverseHandler.upgradeDBVersion(
                params, customer, Universe.getOrBadRequest(universeUUID)));
    finalizeCanaryUpgradeWithFaultInjection(universeUUID);
  }

  @Test
  public void testPG15CanarySoftwareUpgradeFaultInjectedWithPauses() throws InterruptedException {
    Universe universe = createPg11Universe();
    final UUID universeUUID = universe.getUniverseUUID();
    SoftwareUpgradeParams params = buildCanaryUpgradeParams(universe, PG_15_DB_VERSION);
    runCanaryFaultInjectedUpgradeWithPauses(
        universeUUID,
        () ->
            upgradeUniverseHandler.upgradeDBVersion(
                params, customer, Universe.getOrBadRequest(universeUUID)));
    finalizeCanaryUpgradeWithFaultInjection(universeUUID);
  }

  private void runUpgradeThenFaultInjectedRollback(
      UUID universeUUID, String targetVersion, String rollbackVersion) throws InterruptedException {
    runtimeConfService.setKey(
        customer.getUuid(),
        universeUUID,
        UniverseConfKeys.autoFlagUpdateSleepTimeInMilliSeconds.getKey(),
        "1000ms",
        true);
    SoftwareUpgradeParams upgradeParams = getBaseUpgradeParams();
    upgradeParams.setUniverseUUID(universeUUID);
    upgradeParams.ybSoftwareVersion = targetVersion;
    TaskInfo upgradeTask =
        waitForTask(
            upgradeUniverseHandler.upgradeDBVersion(
                upgradeParams, customer, Universe.getOrBadRequest(universeUUID)),
            WAIT_SLEEP_MS,
            WAIT_MAX_RETRIES);
    assertEquals(Success, upgradeTask.getTaskState());
    assertTrue(
        Universe.getOrBadRequest(universeUUID).getUniverseDetails().isSoftwareRollbackAllowed);

    RollbackUpgradeParams rollbackParams = new RollbackUpgradeParams();
    rollbackParams.setUniverseUUID(universeUUID);
    rollbackParams.expectedUniverseVersion = -1;
    rollbackParams.sleepAfterMasterRestartMillis = 1;
    rollbackParams.sleepAfterTServerRestartMillis = 1;
    rollbackParams.upgradeOption = RollbackUpgradeParams.UpgradeOption.ROLLING_UPGRADE;

    runFaultInjectedUpgrade(
        () ->
            upgradeUniverseHandler.rollbackUpgrade(
                rollbackParams, customer, Universe.getOrBadRequest(universeUUID)),
        () -> {
          Universe rolledBack = Universe.getOrBadRequest(universeUUID);
          assertEquals(
              rollbackVersion,
              rolledBack.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion);
          assertEquals(
              SoftwareUpgradeState.Ready, rolledBack.getUniverseDetails().softwareUpgradeState);
          assertFalse(rolledBack.getUniverseDetails().isSoftwareRollbackAllowed);
          if (rolledBack.getUniverseDetails().getPrimaryCluster().userIntent.replicationFactor
              >= 3) {
            verifyPayload();
          }
          verifyYSQL(rolledBack);
        });
  }

  /** Runs a task with abort at each subtask position, retrying until the task fully succeeds. */
  private void runFaultInjectedUpgrade(Supplier<UUID> submitTask, Runnable verifyFinalState)
      throws InterruptedException {
    setPausePosition(0);
    UUID taskUuid = submitTask.get();
    SubtaskCapture capture = captureSubtasks(taskUuid);
    int abortPosition = capture.getFirstAbortPosition();
    int total = capture.totalSubTaskCount;

    try {
      while (true) {
        clearAbortOrPausePositions();
        // Never abort before the final UniverseUpdateSucceeded subtask (position total-1).
        // Aborting after UpdateUniverseSoftwareUpgradeState(Ready) leaves the universe in
        // Ready, which is not retryable for finalize. Matches verifyTaskRetries convention.
        boolean lastRun = abortPosition >= total - 1;
        if (!lastRun) {
          setAbortPosition(abortPosition);
        }
        log.info(
            "Fault injection iteration: abortPosition={}, total={}, lastRun={}",
            abortPosition,
            total,
            lastRun);
        commissioner.resumeTask(taskUuid);
        TaskInfo taskInfo = waitForTask(taskUuid, WAIT_SLEEP_MS, WAIT_MAX_RETRIES);
        if (taskInfo.getTaskState() == TaskInfo.State.Failure) {
          throw new IllegalStateException("Task failed " + getAllErrorsStr(taskInfo));
        }
        if (lastRun) {
          assertEquals(Success, taskInfo.getTaskState());
          break;
        }
        assertEquals(TaskInfo.State.Aborted, taskInfo.getTaskState());
        setPausePosition(0);
        CustomerTask customerTask =
            customerTaskManager.retryCustomerTask(customer.getUuid(), taskUuid);
        taskUuid = customerTask.getTaskUUID();
        capture = captureSubtasks(taskUuid);
        total = capture.totalSubTaskCount;
        abortPosition++;
      }
      verifyFinalState.run();
    } finally {
      clearAbortOrPausePositions();
    }
  }

  /**
   * Drives a canary upgrade through a full per-position abort sweep across every phase while
   * honoring pause/resume at each canary checkpoint. Within a phase the abort position advances by
   * one each iteration (fail at every task); when it reaches the phase's pause checkpoint the run
   * is allowed to pause, the canary upgrade is resumed, and the sweep continues from the next
   * phase's first subtask. After the final phase (no more pause) the sweep runs to {@code Success}.
   */
  private void runCanaryFaultInjectedUpgradeWithPauses(
      UUID universeUUID, Supplier<UUID> submitInitial) throws InterruptedException {
    setPausePosition(0);
    UUID taskUuid = submitInitial.get();
    SubtaskCapture capture = captureSubtasks(taskUuid);
    int abortPosition = capture.getFirstAbortPosition();
    int total = capture.totalSubTaskCount;

    try {
      while (true) {
        clearAbortOrPausePositions();
        PauseCheckpoint pauseCheckpoint = findPauseCheckpoint(taskUuid);
        int stopPosition = pauseCheckpoint.isPresent() ? pauseCheckpoint.position : total - 1;
        boolean runToStop = abortPosition >= stopPosition;
        if (!runToStop) {
          setAbortPosition(abortPosition);
        }
        log.info(
            "Canary fault-injection iteration: abortPosition={}, stopPosition={}, total={},"
                + " pauseCheckpoint={}, canaryPauseState={}, runToStop={}",
            abortPosition,
            stopPosition,
            total,
            pauseCheckpoint.position,
            pauseCheckpoint.canaryPauseState,
            runToStop);
        commissioner.resumeTask(taskUuid);
        TaskInfo taskInfo = waitForTask(taskUuid, WAIT_SLEEP_MS, WAIT_MAX_RETRIES);
        if (taskInfo.getTaskState() == TaskInfo.State.Failure) {
          throw new IllegalStateException("Task failed " + getAllErrorsStr(taskInfo));
        }
        if (taskInfo.getTaskState() == TaskInfo.State.Paused) {
          assertEquals(
              SoftwareUpgradeState.Paused,
              Universe.getOrBadRequest(universeUUID).getUniverseDetails().softwareUpgradeState);
          final UUID pausedTaskUuid = taskUuid;
          setPausePosition(0);
          taskUuid =
              upgradeUniverseHandler.resumeCanarySoftwareUpgrade(
                  customer.getUuid(), universeUUID, pausedTaskUuid);
          capture = captureSubtasks(taskUuid);
          total = capture.totalSubTaskCount;
          abortPosition = capture.getFirstAbortPosition();
          continue;
        }
        if (taskInfo.getTaskState() == Success) {
          break;
        }
        assertEquals(TaskInfo.State.Aborted, taskInfo.getTaskState());
        setPausePosition(0);
        CustomerTask customerTask =
            customerTaskManager.retryCustomerTask(customer.getUuid(), taskUuid);
        taskUuid = customerTask.getTaskUUID();
        capture = captureSubtasks(taskUuid);
        total = capture.totalSubTaskCount;
        abortPosition++;
      }
    } finally {
      clearAbortOrPausePositions();
    }
  }

  /**
   * Runs the post-upgrade finalize phase with a full per-position abort sweep and verifies the
   * universe reaches {@code Ready} with payload/YSQL intact.
   */
  private void finalizeCanaryUpgradeWithFaultInjection(UUID universeUUID)
      throws InterruptedException {
    assertEquals(
        SoftwareUpgradeState.PreFinalize,
        Universe.getOrBadRequest(universeUUID).getUniverseDetails().softwareUpgradeState);

    FinalizeUpgradeParams finalizeParams = new FinalizeUpgradeParams();
    finalizeParams.setUniverseUUID(universeUUID);
    finalizeParams.expectedUniverseVersion = -1;
    runFaultInjectedUpgrade(
        () ->
            upgradeUniverseHandler.finalizeUpgrade(
                finalizeParams, customer, Universe.getOrBadRequest(universeUUID)),
        () -> {
          Universe finalized = Universe.getOrBadRequest(universeUUID);
          assertEquals(
              SoftwareUpgradeState.Ready, finalized.getUniverseDetails().softwareUpgradeState);
          assertFalse(finalized.getUniverseDetails().isSoftwareRollbackAllowed);
          verifyPayload();
          verifyYSQL(finalized);
        });
  }

  private PauseCheckpoint findPauseCheckpoint(UUID taskUuid) {
    return TaskInfo.getOrBadRequest(taskUuid).getSubTasks().stream()
        .filter(t -> t.getTaskType() == TaskType.SaveSoftwareUpgradeProgress)
        .filter(t -> t.getTaskParams().path("pauseAfter").asBoolean(false))
        .min((a, b) -> Integer.compare(a.getPosition(), b.getPosition()))
        .map(
            t ->
                new PauseCheckpoint(
                    t.getPosition(),
                    CanaryPauseState.valueOf(t.getTaskParams().path("canaryPauseState").asText())))
        .orElse(new PauseCheckpoint(-1, null));
  }

  private SubtaskCapture captureSubtasks(UUID taskUuid) throws InterruptedException {
    waitForTaskPaused(taskUuid);
    TaskInfo taskInfo = TaskInfo.getOrBadRequest(taskUuid);
    Optional<Integer> freezePosition =
        taskInfo.getSubTasks().stream()
            .filter(t -> t.getTaskType() == TaskType.FreezeUniverse)
            .map(TaskInfo::getPosition)
            .findFirst();
    if (freezePosition.isPresent()) {
      setPausePosition(freezePosition.get() + 1);
      commissioner.resumeTask(taskUuid);
      waitForTaskPaused(taskUuid);
      taskInfo.refresh();
    }
    Map<Integer, List<TaskInfo>> subtaskMap =
        taskInfo.getSubTasks().stream()
            .collect(
                Collectors.groupingBy(TaskInfo::getPosition, TreeMap::new, Collectors.toList()));
    int minExpectedSize = freezePosition.isPresent() ? freezePosition.get() + 2 : 2;
    assertTrue("At least some real subtasks must be present", subtaskMap.size() >= minExpectedSize);
    return new SubtaskCapture(subtaskMap.size(), freezePosition.orElse(-1));
  }

  private Universe createRollbackCapableUniverse() throws InterruptedException {
    return createRollbackCapableUniverse(3, 3);
  }

  private Universe createRollbackCapableUniverse(int rf, int numNodes) throws InterruptedException {
    updateProviderDetailsForCreateUniverse(OLD_VERSION_WITH_ROLLBACK);
    UniverseDefinitionTaskParams.UserIntent userIntent =
        getDefaultUserIntent(null, false, rf, numNodes);
    userIntent.ybSoftwareVersion = OLD_VERSION_WITH_ROLLBACK;
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    Universe universe = createUniverse(userIntent);
    initYSQL(universe);
    initAndStartPayload(universe);
    disableNodesAreSafeToTakeDown(universe);
    return universe;
  }

  private Universe createPg11Universe() throws InterruptedException {
    updateProviderDetailsForCreateUniverse(PG_11_DB_VERSION);
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.ybSoftwareVersion = PG_11_DB_VERSION;
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    Universe universe = createUniverse(userIntent);
    initYSQL(universe);
    initAndStartPayload(universe);
    disableNodesAreSafeToTakeDown(universe);
    runtimeConfService.setKey(
        customer.getUuid(),
        universe.getUniverseUUID(),
        UniverseConfKeys.allowDowngrades.getKey(),
        "true",
        true);
    return universe;
  }

  private void disableNodesAreSafeToTakeDown(Universe universe) {
    runtimeConfService.setKey(
        customer.getUuid(),
        universe.getUniverseUUID(),
        UniverseConfKeys.useNodesAreSafeToTakeDown.getKey(),
        "false",
        true);
  }

  private void enableCanaryUpgrade(Universe universe) {
    runtimeConfService.setKey(
        customer.getUuid(),
        universe.getUniverseUUID(),
        UniverseConfKeys.enableCanaryUpgrade.getKey(),
        "true",
        true);
  }

  private SoftwareUpgradeParams buildCanaryUpgradeParams(Universe universe, String targetVersion) {
    UUID universeUUID = universe.getUniverseUUID();
    enableCanaryUpgrade(universe);
    UniverseDefinitionTaskParams.Cluster primaryCluster =
        Universe.getOrBadRequest(universeUUID).getUniverseDetails().getPrimaryCluster();
    List<UUID> azOrder =
        PlacementInfoUtil.getAZsSortedByNumNodes(primaryCluster.getOverallPlacement()).stream()
            .map(az -> az.uuid)
            .collect(Collectors.toList());
    assertTrue("Expected multi-AZ placement for canary AZ steps", azOrder.size() >= 3);
    UUID firstAz = azOrder.get(0);

    List<AZUpgradeStep> steps = new ArrayList<>();
    for (UUID azUuid : azOrder) {
      AZUpgradeStep step = new AZUpgradeStep();
      step.azUUID = azUuid;
      step.pauseAfterTserverUpgrade = azUuid.equals(firstAz);
      steps.add(step);
    }

    SoftwareUpgradeParams params = getBaseUpgradeParams();
    params.setUniverseUUID(universeUUID);
    params.ybSoftwareVersion = targetVersion;
    params.sleepAfterMasterRestartMillis = 2000;
    params.sleepAfterTServerRestartMillis = 2000;
    params.clusters.add(primaryCluster);
    params.canaryUpgradeConfig = new CanaryUpgradeConfig();
    params.canaryUpgradeConfig.pauseAfterMasters = true;
    params.canaryUpgradeConfig.primaryClusterAZSteps = steps;
    return params;
  }

  private SoftwareUpgradeParams getBaseUpgradeParams() {
    SoftwareUpgradeParams params = new SoftwareUpgradeParams();
    params.expectedUniverseVersion = -1;
    params.sleepAfterMasterRestartMillis = 1000;
    params.sleepAfterTServerRestartMillis = 1000;
    return params;
  }

  private void addRelease(String dbVersion, String dbVersionUrl) {
    String downloadURL = String.format(dbVersionUrl, os, arch);
    downloadAndSetUpYBSoftware(os, arch, downloadURL, dbVersion);
    ObjectNode releases =
        (ObjectNode) YugawareProperty.get(ReleaseManager.CONFIG_TYPE.name()).getValue();
    releases.set(dbVersion, getMetadataJson(dbVersion, false).get(dbVersion));
    YugawareProperty.addConfigProperty(ReleaseManager.CONFIG_TYPE.name(), releases, "release");
  }

  private void updateProviderDetailsForCreateUniverse(String dbVersion) {
    ybVersion = dbVersion;
    ybBinPath = deriveYBBinPath(dbVersion);
    LocalCloudInfo localCloudInfo = new LocalCloudInfo();
    localCloudInfo.setDataHomeDir(
        ((LocalCloudInfo) CloudInfoInterface.get(provider)).getDataHomeDir());
    localCloudInfo.setYugabyteBinDir(ybBinPath);
    localCloudInfo.setYbcBinDir(ybcBinPath);
    ProviderDetails.CloudInfo cloudInfo = new ProviderDetails.CloudInfo();
    cloudInfo.setLocal(localCloudInfo);
    ProviderDetails providerDetails = new ProviderDetails();
    providerDetails.setCloudInfo(cloudInfo);
    provider.setDetails(providerDetails);
    provider.update();
  }
}
