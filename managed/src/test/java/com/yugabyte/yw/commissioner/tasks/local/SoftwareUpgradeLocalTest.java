// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.local;

import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.hamcrest.CoreMatchers.containsString;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ReleaseManager;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.controllers.UniverseControllerRequestBinder;
import com.yugabyte.yw.forms.AZUpgradeState;
import com.yugabyte.yw.forms.AZUpgradeStatus;
import com.yugabyte.yw.forms.AZUpgradeStep;
import com.yugabyte.yw.forms.CanaryUpgradeConfig;
import com.yugabyte.yw.forms.FinalizeUpgradeParams;
import com.yugabyte.yw.forms.GFlagsUpgradeParams;
import com.yugabyte.yw.forms.RollbackUpgradeParams;
import com.yugabyte.yw.forms.SoftwareUpgradeParams;
import com.yugabyte.yw.forms.SoftwareUpgradeProgress;
import com.yugabyte.yw.forms.UniverseConfigureTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.PrevYBSoftwareConfig;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.SoftwareUpgradeState;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.ProviderDetails;
import com.yugabyte.yw.models.ScopedRuntimeConfig;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.YugawareProperty;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.provider.LocalCloudInfo;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import org.junit.Before;
import org.junit.Test;
import play.libs.Json;

public class SoftwareUpgradeLocalTest extends LocalProviderUniverseTestBase {

  @Override
  protected Pair<Integer, Integer> getIpRange() {
    return new Pair<>(180, 210);
  }

  public static final String OLD_DB_VERSION = "2.20.0.1-b1";
  public static String OLD_DB_VERSION_URL =
      "https://s3.us-west-2.amazonaws.com/uploads.dev.yugabyte.com/"
          + "local-provider-test/2.20.0.1-b1/yugabyte-2.20.0.1-b1-%s-%s.tar.gz";

  public static final String OLD_VERSION_WITH_ROLLBACK = "2.21.0.0-b340";
  private static final String OLD_VERSION_WTH_ROLLBACK_URL =
      "https://s3.us-west-2.amazonaws.com/uploads.dev.yugabyte.com/"
          + "local-provider-test/2.21.0.0-b340/yugabyte-2.21.0.0-b340-%s-%s.tar.gz";

  private static final String PG_15_DB_VERSION = "2025.1.1.0-b73";
  private static final String PG_15_DB_VERSION_URL =
      "https://s3.us-west-2.amazonaws.com/uploads.dev.yugabyte.com/"
          + "local-provider-test/2025.1.1.0-b73/yugabyte-2025.1.1.0-b73-%s-%s.tar.gz";

  private static final String PG_11_DB_VERSION = "2024.2.3.0-b116";
  private static final String PG_11_DB_VERSION_URL =
      "https://software.yugabyte.com/releases/2024.2.3.0/yugabyte-2024.2.3.0-b116-%s-%s.tar.gz";

  @Before
  public void setup() {
    addRelease(OLD_VERSION_WITH_ROLLBACK, OLD_VERSION_WTH_ROLLBACK_URL);
    addRelease(OLD_DB_VERSION, OLD_DB_VERSION_URL);
    addRelease(PG_11_DB_VERSION, PG_11_DB_VERSION_URL);
    addRelease(PG_15_DB_VERSION, PG_15_DB_VERSION_URL);
    localNodeManager.addVersionBinPath(
        OLD_VERSION_WITH_ROLLBACK,
        baseDir + "/yugabyte/yugabyte-" + OLD_VERSION_WITH_ROLLBACK + "/bin");
    localNodeManager.addVersionBinPath(
        OLD_DB_VERSION, baseDir + "/yugabyte/yugabyte-" + OLD_DB_VERSION + "/bin");

    localNodeManager.addVersionBinPath(
        PG_15_DB_VERSION, baseDir + "/yugabyte/yugabyte-" + PG_15_DB_VERSION + "/bin");
    localNodeManager.addVersionBinPath(
        PG_11_DB_VERSION, baseDir + "/yugabyte/yugabyte-" + PG_11_DB_VERSION + "/bin");

    runtimeConfService.setKey(
        customer.getUuid(),
        ScopedRuntimeConfig.GLOBAL_SCOPE_UUID,
        GlobalConfKeys.skipVersionChecks.getKey(),
        "true",
        true);
  }

  protected SoftwareUpgradeParams getBaseUpgradeParams() {
    SoftwareUpgradeParams params = new SoftwareUpgradeParams();
    params.expectedUniverseVersion = -1;
    params.sleepAfterMasterRestartMillis = 10000;
    params.sleepAfterTServerRestartMillis = 10000;
    return params;
  }

  @Test
  public void testSoftwareUpgradeWithNoRollbackSupport() throws InterruptedException {
    updateProviderDetailsForCreateUniverse(OLD_DB_VERSION);
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.ybSoftwareVersion = OLD_DB_VERSION;
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    Universe universe = createUniverse(userIntent);
    initAndStartPayload(universe);
    SoftwareUpgradeParams params = getBaseUpgradeParams();
    params.setUniverseUUID(universe.getUniverseUUID());
    params.ybSoftwareVersion = PG_11_DB_VERSION;
    TaskInfo taskInfo =
        waitForTask(
            upgradeUniverseHandler.upgradeDBVersion(
                params, customer, Universe.getOrBadRequest(universe.getUniverseUUID())));
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    assertEquals(SoftwareUpgradeState.Ready, universe.getUniverseDetails().softwareUpgradeState);
    assertFalse(universe.getUniverseDetails().isSoftwareRollbackAllowed);
    verifyPayload();

    Optional<ObjectNode> doneStatus = getTaskStatus(taskInfo.getUuid());
    assertTrue(doneStatus.isPresent());
    assertFalse(
        "Task status API should omit softwareUpgradeProgress after a successful standard upgrade",
        doneStatus.get().has("softwareUpgradeProgress"));
  }

  @Test
  public void testRollbackUpgrade() throws InterruptedException {
    updateProviderDetailsForCreateUniverse(OLD_VERSION_WITH_ROLLBACK);
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.ybSoftwareVersion = OLD_VERSION_WITH_ROLLBACK;
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    Universe universe = createUniverse(userIntent);
    initAndStartPayload(universe);
    runtimeConfService.setKey(
        customer.getUuid(),
        universe.getUniverseUUID(),
        UniverseConfKeys.useNodesAreSafeToTakeDown.getKey(),
        "false",
        true);
    SoftwareUpgradeParams params = getBaseUpgradeParams();
    params.ybSoftwareVersion = PG_11_DB_VERSION;
    params.setUniverseUUID(universe.getUniverseUUID());
    TaskInfo taskInfo =
        waitForTask(
            upgradeUniverseHandler.upgradeDBVersion(
                params, customer, Universe.getOrBadRequest(universe.getUniverseUUID())));
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    assertTrue(universe.getUniverseDetails().isSoftwareRollbackAllowed);
    RollbackUpgradeParams rollbackParams = new RollbackUpgradeParams();
    rollbackParams.setUniverseUUID(universe.getUniverseUUID());
    rollbackParams.expectedUniverseVersion = -1;
    taskInfo =
        waitForTask(
            upgradeUniverseHandler.rollbackUpgrade(
                rollbackParams, customer, Universe.getOrBadRequest(universe.getUniverseUUID())));
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    assertEquals(SoftwareUpgradeState.Ready, universe.getUniverseDetails().softwareUpgradeState);
    assertFalse(universe.getUniverseDetails().isSoftwareRollbackAllowed);
    verifyPayload();
  }

  @Test
  public void testFinalizeUpgrade() throws InterruptedException {
    updateProviderDetailsForCreateUniverse(OLD_VERSION_WITH_ROLLBACK);
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    userIntent.ybSoftwareVersion = OLD_VERSION_WITH_ROLLBACK;
    Universe universe = createUniverse(userIntent);
    initAndStartPayload(universe);
    runtimeConfService.setKey(
        customer.getUuid(),
        universe.getUniverseUUID(),
        UniverseConfKeys.useNodesAreSafeToTakeDown.getKey(),
        "false",
        true);
    SoftwareUpgradeParams params = getBaseUpgradeParams();
    params.ybSoftwareVersion = PG_11_DB_VERSION;
    params.setUniverseUUID(universe.getUniverseUUID());
    TaskInfo taskInfo =
        waitForTask(
            upgradeUniverseHandler.upgradeDBVersion(
                params, customer, Universe.getOrBadRequest(universe.getUniverseUUID())));
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    assertTrue(universe.getUniverseDetails().isSoftwareRollbackAllowed);
    verifyPayload();
    if (!universe.getUniverseDetails().softwareUpgradeState.equals(SoftwareUpgradeState.Ready)) {
      FinalizeUpgradeParams finalizeUpgradeParams = new FinalizeUpgradeParams();
      finalizeUpgradeParams.setUniverseUUID(universe.getUniverseUUID());
      finalizeUpgradeParams.expectedUniverseVersion = -1;
      taskInfo =
          waitForTask(
              upgradeUniverseHandler.finalizeUpgrade(
                  finalizeUpgradeParams,
                  customer,
                  Universe.getOrBadRequest(universe.getUniverseUUID())));
      assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
      universe = Universe.getOrBadRequest(universe.getUniverseUUID());
      assertEquals(SoftwareUpgradeState.Ready, universe.getUniverseDetails().softwareUpgradeState);
      assertFalse(universe.getUniverseDetails().isSoftwareRollbackAllowed);
    }
  }

  @Test
  public void testPG15SoftwareUpgradeRollback() throws InterruptedException {
    updateProviderDetailsForCreateUniverse(PG_11_DB_VERSION);
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    userIntent.ybSoftwareVersion = PG_11_DB_VERSION;
    Universe universe = createUniverse(userIntent);
    initAndStartPayload(universe);
    runtimeConfService.setKey(
        customer.getUuid(),
        universe.getUniverseUUID(),
        UniverseConfKeys.useNodesAreSafeToTakeDown.getKey(),
        "false",
        true);
    runtimeConfService.setKey(
        customer.getUuid(),
        universe.getUniverseUUID(),
        UniverseConfKeys.allowDowngrades.getKey(),
        "true",
        true);
    SoftwareUpgradeParams params = getBaseUpgradeParams();
    params.ybSoftwareVersion = PG_15_DB_VERSION;
    params.setUniverseUUID(universe.getUniverseUUID());
    TaskInfo taskInfo =
        waitForTask(
            upgradeUniverseHandler.upgradeDBVersion(
                params, customer, Universe.getOrBadRequest(universe.getUniverseUUID())));
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    assertTrue(universe.getUniverseDetails().isSoftwareRollbackAllowed);
    RollbackUpgradeParams rollbackParams = new RollbackUpgradeParams();
    rollbackParams.setUniverseUUID(universe.getUniverseUUID());
    rollbackParams.expectedUniverseVersion = -1;
    taskInfo =
        waitForTask(
            upgradeUniverseHandler.rollbackUpgrade(
                rollbackParams, customer, Universe.getOrBadRequest(universe.getUniverseUUID())));
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    assertEquals(SoftwareUpgradeState.Ready, universe.getUniverseDetails().softwareUpgradeState);
    assertFalse(universe.getUniverseDetails().isSoftwareRollbackAllowed);
    verifyPayload();
  }

  @Test
  public void testPG15SoftwareUpgradeFinalize() throws InterruptedException {
    updateProviderDetailsForCreateUniverse(PG_11_DB_VERSION);
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    userIntent.ybSoftwareVersion = PG_11_DB_VERSION;
    userIntent.enableYSQL = true;
    userIntent.ysqlPassword = "password&123";
    Universe universe = createUniverse(userIntent);
    initAndStartPayload(universe);
    runtimeConfService.setKey(
        customer.getUuid(),
        universe.getUniverseUUID(),
        UniverseConfKeys.useNodesAreSafeToTakeDown.getKey(),
        "false",
        true);
    runtimeConfService.setKey(
        customer.getUuid(),
        universe.getUniverseUUID(),
        UniverseConfKeys.allowDowngrades.getKey(),
        "true",
        true);
    SoftwareUpgradeParams params = getBaseUpgradeParams();
    params.ybSoftwareVersion = PG_15_DB_VERSION;
    params.setUniverseUUID(universe.getUniverseUUID());
    TaskInfo taskInfo =
        waitForTask(
            upgradeUniverseHandler.upgradeDBVersion(
                params, customer, Universe.getOrBadRequest(universe.getUniverseUUID())));
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    assertTrue(universe.getUniverseDetails().isSoftwareRollbackAllowed);
    FinalizeUpgradeParams finalizeUpgradeParams = new FinalizeUpgradeParams();
    finalizeUpgradeParams.setUniverseUUID(universe.getUniverseUUID());
    finalizeUpgradeParams.expectedUniverseVersion = -1;
    taskInfo =
        waitForTask(
            upgradeUniverseHandler.finalizeUpgrade(
                finalizeUpgradeParams,
                customer,
                Universe.getOrBadRequest(universe.getUniverseUUID())));
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    assertEquals(SoftwareUpgradeState.Ready, universe.getUniverseDetails().softwareUpgradeState);
    assertFalse(universe.getUniverseDetails().isSoftwareRollbackAllowed);
  }

  @Test
  public void testCanarySoftwareUpgradeMasterPauseThenFirstAzPause() throws InterruptedException {
    updateProviderDetailsForCreateUniverse(OLD_VERSION_WITH_ROLLBACK);
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.ybSoftwareVersion = OLD_VERSION_WITH_ROLLBACK;
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    Universe universe = createUniverse(userIntent);
    initAndStartPayload(universe);
    runtimeConfService.setKey(
        customer.getUuid(),
        universe.getUniverseUUID(),
        UniverseConfKeys.useNodesAreSafeToTakeDown.getKey(),
        "false",
        true);
    runtimeConfService.setKey(
        customer.getUuid(),
        universe.getUniverseUUID(),
        UniverseConfKeys.enableCanaryUpgrade.getKey(),
        "true",
        true);

    UniverseDefinitionTaskParams.Cluster primaryCluster =
        universe.getUniverseDetails().getPrimaryCluster();
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
    params.setUniverseUUID(universe.getUniverseUUID());
    params.ybSoftwareVersion = PG_11_DB_VERSION;
    params.sleepAfterMasterRestartMillis = 2000;
    params.sleepAfterTServerRestartMillis = 2000;
    params.clusters.add(primaryCluster);
    params.canaryUpgradeConfig = new CanaryUpgradeConfig();
    params.canaryUpgradeConfig.pauseAfterMasters = true;
    params.canaryUpgradeConfig.primaryClusterAZSteps = steps;

    UUID taskUuid =
        upgradeUniverseHandler.upgradeDBVersion(
            params, customer, Universe.getOrBadRequest(universe.getUniverseUUID()));
    TaskInfo taskInfo = waitForTask(taskUuid, 500, 3600);
    assertEquals(TaskInfo.State.Paused, taskInfo.getTaskState());

    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    assertEquals(SoftwareUpgradeState.Paused, universe.getUniverseDetails().softwareUpgradeState);
    PrevYBSoftwareConfig prev = universe.getUniverseDetails().prevYBSoftwareConfig;
    assertNotNull(prev);
    assertTrue(allMasterAzsCompleted(prev));
    assertTrue(
        "No tserver AZs completed yet after master pause",
        countPrimaryCompletedTserverAzs(prev, universe) == 0);

    assertNotNull(prev.getTserverAZUpgradeStatesList());
    assertFalse(
        "Expected non-empty tserver AZ progress after initial snapshot",
        prev.getTserverAZUpgradeStatesList().isEmpty());
    UUID primaryUuid = primaryCluster.uuid;
    for (AZUpgradeState s : prev.getTserverAZUpgradeStatesList()) {
      if (primaryUuid.equals(s.getClusterUUID())) {
        assertEquals(
            "Tserver AZs should be NOT_STARTED after master pause",
            AZUpgradeStatus.NOT_STARTED,
            s.getStatus());
      }
    }

    assertTaskStatusSoftwareUpgradeProgressMatchesUniverse(taskUuid, universe);

    UUID resumedUuid =
        upgradeUniverseHandler.resumeCanarySoftwareUpgrade(
            customer.getUuid(), universe.getUniverseUUID(), taskUuid);
    taskInfo = waitForTask(resumedUuid, 500, 3600);
    assertEquals(TaskInfo.State.Paused, taskInfo.getTaskState());

    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    assertEquals(SoftwareUpgradeState.Paused, universe.getUniverseDetails().softwareUpgradeState);
    prev = universe.getUniverseDetails().prevYBSoftwareConfig;
    assertNotNull(prev);
    assertTrue(
        "First AZ should be in completed AZs after tserver pause",
        primaryTserverAzCompleted(prev, universe, firstAz));

    assertTaskStatusSoftwareUpgradeProgressMatchesUniverse(resumedUuid, universe);

    UUID resumed2Uuid =
        upgradeUniverseHandler.resumeCanarySoftwareUpgrade(
            customer.getUuid(), universe.getUniverseUUID(), resumedUuid);
    TaskInfo finalTask = waitForTask(resumed2Uuid, universe);
    assertEquals(Success, finalTask.getTaskState());

    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    assertEquals(
        SoftwareUpgradeState.PreFinalize, universe.getUniverseDetails().softwareUpgradeState);

    Optional<ObjectNode> finalStatus = getTaskStatus(finalTask.getUuid());
    assertTrue(finalStatus.isPresent());
    assertFalse(
        "Task status should omit softwareUpgradeProgress after software upgrade phase completes",
        finalStatus.get().has("softwareUpgradeProgress"));
  }

  /**
   * Canary upgrade with {@link CanaryUpgradeConfig#pauseAfterMasters} and no per-AZ tserver pauses,
   * left in {@link SoftwareUpgradeState#Paused} after masters complete.
   */
  private Universe createUniversePausedAfterCanaryMasterPhase() throws InterruptedException {
    updateProviderDetailsForCreateUniverse(OLD_VERSION_WITH_ROLLBACK);
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.ybSoftwareVersion = OLD_VERSION_WITH_ROLLBACK;
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    Universe universe = createUniverse(userIntent);
    initAndStartPayload(universe);
    runtimeConfService.setKey(
        customer.getUuid(),
        universe.getUniverseUUID(),
        UniverseConfKeys.useNodesAreSafeToTakeDown.getKey(),
        "false",
        true);
    runtimeConfService.setKey(
        customer.getUuid(),
        universe.getUniverseUUID(),
        UniverseConfKeys.enableCanaryUpgrade.getKey(),
        "true",
        true);

    UniverseDefinitionTaskParams.Cluster primaryCluster =
        universe.getUniverseDetails().getPrimaryCluster();
    List<UUID> azOrder =
        PlacementInfoUtil.getAZsSortedByNumNodes(primaryCluster.getOverallPlacement()).stream()
            .map(az -> az.uuid)
            .collect(Collectors.toList());
    assertTrue("Expected multi-AZ placement for canary AZ steps", azOrder.size() >= 3);

    List<AZUpgradeStep> steps = new ArrayList<>();
    for (UUID azUuid : azOrder) {
      AZUpgradeStep step = new AZUpgradeStep();
      step.azUUID = azUuid;
      step.pauseAfterTserverUpgrade = false;
      steps.add(step);
    }

    SoftwareUpgradeParams params = getBaseUpgradeParams();
    params.setUniverseUUID(universe.getUniverseUUID());
    params.ybSoftwareVersion = PG_11_DB_VERSION;
    params.sleepAfterMasterRestartMillis = 2000;
    params.sleepAfterTServerRestartMillis = 2000;
    params.clusters.add(primaryCluster);
    params.canaryUpgradeConfig = new CanaryUpgradeConfig();
    params.canaryUpgradeConfig.pauseAfterMasters = true;
    params.canaryUpgradeConfig.primaryClusterAZSteps = steps;

    UUID taskUuid =
        upgradeUniverseHandler.upgradeDBVersion(
            params, customer, Universe.getOrBadRequest(universe.getUniverseUUID()));
    TaskInfo taskInfo = waitForTask(taskUuid, 500, 3600);
    assertEquals(TaskInfo.State.Paused, taskInfo.getTaskState());

    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    assertEquals(SoftwareUpgradeState.Paused, universe.getUniverseDetails().softwareUpgradeState);
    return universe;
  }

  @Test
  public void testCanaryUpgradePauseThenRollback() throws InterruptedException {
    Universe universe = createUniversePausedAfterCanaryMasterPhase();

    RollbackUpgradeParams rollbackParams = new RollbackUpgradeParams();
    rollbackParams.setUniverseUUID(universe.getUniverseUUID());
    rollbackParams.expectedUniverseVersion = -1;
    TaskInfo rollbackTask =
        waitForTask(
            upgradeUniverseHandler.rollbackUpgrade(
                rollbackParams, customer, Universe.getOrBadRequest(universe.getUniverseUUID())));
    assertEquals(Success, rollbackTask.getTaskState());

    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    assertEquals(SoftwareUpgradeState.Ready, universe.getUniverseDetails().softwareUpgradeState);
    assertEquals(
        OLD_VERSION_WITH_ROLLBACK,
        universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion);
    verifyPayload();
  }

  @Test
  public void testCanaryPauseBlocksGFlagsAndEditUniverse() throws InterruptedException {
    Universe universe = createUniversePausedAfterCanaryMasterPhase();

    runtimeConfService.setKey(
        customer.getUuid(),
        universe.getUniverseUUID(),
        UniverseConfKeys.allowGFlagsOverrideDuringPreFinalize.getKey(),
        "true",
        true);

    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    GFlagsUpgradeParams gFlagsUpgradeParams =
        UniverseControllerRequestBinder.deepCopy(
            universe.getUniverseDetails(), GFlagsUpgradeParams.class);
    gFlagsUpgradeParams.setUniverseUUID(universe.getUniverseUUID());
    gFlagsUpgradeParams.upgradeOption = UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE;
    gFlagsUpgradeParams.expectedUniverseVersion = universe.getVersion();
    gFlagsUpgradeParams.clusters = universe.getUniverseDetails().clusters;
    gFlagsUpgradeParams.sleepAfterMasterRestartMillis = 10000;
    gFlagsUpgradeParams.sleepAfterTServerRestartMillis = 10000;
    Map<String, String> master = new HashMap<>(GFLAGS);
    master.put("load_balancer_max_over_replicated_tablets", "16");
    Map<String, String> tserver = new HashMap<>(GFLAGS);
    tserver.put("load_balancer_max_over_replicated_tablets", "16");
    gFlagsUpgradeParams.getPrimaryCluster().userIntent.specificGFlags =
        SpecificGFlags.construct(master, tserver);

    // After canary pause the upgrade task leaves the per-universe queue; the next task runs
    // immediately and validateParams fails inside TaskExecutor.submit before a UUID is returned,
    // so the handler throws PlatformServiceException synchronously (not an async failed task).
    final UUID gflagsUniverseUuid = universe.getUniverseUUID();
    PlatformServiceException gflagsEx =
        assertThrows(
            PlatformServiceException.class,
            () ->
                upgradeUniverseHandler.upgradeGFlags(
                    gFlagsUpgradeParams, customer, Universe.getOrBadRequest(gflagsUniverseUuid)));
    assertThat(gflagsEx.getMessage(), containsString("paused canary software upgrade"));

    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    changeNumberOfNodesInPrimary(universe, 1);
    final Universe universeForEdit = universe;
    PlatformServiceException editEx =
        assertThrows(
            PlatformServiceException.class,
            () ->
                universeCRUDHandler.update(
                    customer,
                    Universe.getOrBadRequest(universeForEdit.getUniverseUUID()),
                    universeForEdit.getUniverseDetails()));
    assertThat(editEx.getMessage(), containsString("paused canary software upgrade"));

    RollbackUpgradeParams rollbackParams = new RollbackUpgradeParams();
    rollbackParams.setUniverseUUID(universe.getUniverseUUID());
    rollbackParams.expectedUniverseVersion = -1;
    TaskInfo rollbackTask =
        waitForTask(
            upgradeUniverseHandler.rollbackUpgrade(
                rollbackParams, customer, Universe.getOrBadRequest(universe.getUniverseUUID())));
    assertEquals(Success, rollbackTask.getTaskState());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    assertEquals(SoftwareUpgradeState.Ready, universe.getUniverseDetails().softwareUpgradeState);
  }

  private void changeNumberOfNodesInPrimary(Universe universe, int increment) {
    UniverseDefinitionTaskParams.Cluster cluster =
        universe.getUniverseDetails().getPrimaryCluster();
    cluster.userIntent.numNodes += increment;
    PlacementInfoUtil.updateUniverseDefinition(
        universe.getUniverseDetails(),
        customer.getId(),
        cluster.uuid,
        UniverseConfigureTaskParams.ClusterOperationType.EDIT);
  }

  /**
   * Task status JSON matching {@link com.yugabyte.yw.commissioner.Commissioner#buildTaskStatus}
   * without {@link com.yugabyte.yw.commissioner.Commissioner#mayGetStatus}, which uses PostgreSQL
   * {@code ::jsonb} SQL that H2 cannot run in local tests.
   */
  private Optional<ObjectNode> getTaskStatus(UUID taskUUID) {
    CustomerTask task = CustomerTask.find.query().where().eq("task_uuid", taskUUID).findOne();
    if (task == null) {
      return Optional.empty();
    }
    TaskInfo taskInfo = task.getTaskInfo();
    Map<UUID, Set<String>> updatingTasks = new HashMap<>();
    Map<UUID, CustomerTask> lastTaskByTarget = new HashMap<>();
    lastTaskByTarget.put(
        task.getTargetUUID(), CustomerTask.getLastTaskByTargetUuid(task.getTargetUUID()));
    return commissioner.buildTaskStatus(
        task, taskInfo.getSubTasks(), updatingTasks, lastTaskByTarget);
  }

  /**
   * Ensures task status (via {@link #getTaskStatus}) exposes the same {@link
   * SoftwareUpgradeProgress} as {@link SoftwareUpgradeProgress#fromPrevYBSoftwareConfigIfPresent}
   * on the universe (task monitoring / UI poll path).
   */
  private void assertTaskStatusSoftwareUpgradeProgressMatchesUniverse(
      UUID taskUuid, Universe universe) {
    PrevYBSoftwareConfig prev = universe.getUniverseDetails().prevYBSoftwareConfig;
    SoftwareUpgradeProgress expected =
        SoftwareUpgradeProgress.fromPrevYBSoftwareConfigIfPresent(prev);
    assertNotNull(expected);
    Optional<ObjectNode> opt = getTaskStatus(taskUuid);
    assertTrue(opt.isPresent());
    assertTrue(
        "Task status should include softwareUpgradeProgress while upgrade is in progress",
        opt.get().has("softwareUpgradeProgress"));
    SoftwareUpgradeProgress actual =
        Json.fromJson(opt.get().get("softwareUpgradeProgress"), SoftwareUpgradeProgress.class);
    assertEquals(expected, actual);
  }

  private void addRelease(String dbVersion, String dbVersionUrl) {
    String downloadURL = String.format(dbVersionUrl, os, arch);
    downloadAndSetUpYBSoftware(os, arch, downloadURL, dbVersion);
    ObjectNode releases =
        (ObjectNode) YugawareProperty.get(ReleaseManager.CONFIG_TYPE.name()).getValue();
    releases.set(dbVersion, getMetadataJson(dbVersion, false).get(dbVersion));
    YugawareProperty.addConfigProperty(ReleaseManager.CONFIG_TYPE.name(), releases, "release");
  }

  private static boolean allMasterAzsCompleted(PrevYBSoftwareConfig prev) {
    if (prev.getMasterAZUpgradeStatesList() == null
        || prev.getMasterAZUpgradeStatesList().isEmpty()) {
      return false;
    }
    return prev.getMasterAZUpgradeStatesList().stream()
        .allMatch(s -> s.getStatus() == AZUpgradeStatus.COMPLETED);
  }

  private static boolean primaryTserverAzCompleted(
      PrevYBSoftwareConfig prev, Universe universe, UUID azUuid) {
    UUID primaryUuid = universe.getUniverseDetails().getPrimaryCluster().uuid;
    if (prev.getTserverAZUpgradeStatesList() == null) {
      return false;
    }
    return prev.getTserverAZUpgradeStatesList().stream()
        .anyMatch(
            s ->
                primaryUuid.equals(s.getClusterUUID())
                    && azUuid.equals(s.getAzUUID())
                    && s.getStatus() == AZUpgradeStatus.COMPLETED);
  }

  private static long countPrimaryCompletedTserverAzs(PrevYBSoftwareConfig prev, Universe u) {
    UUID primaryUuid = u.getUniverseDetails().getPrimaryCluster().uuid;
    if (prev.getTserverAZUpgradeStatesList() == null) {
      return 0;
    }
    return prev.getTserverAZUpgradeStatesList().stream()
        .filter(
            s ->
                primaryUuid.equals(s.getClusterUUID())
                    && s.getStatus() == AZUpgradeStatus.COMPLETED)
        .map(AZUpgradeState::getAzUUID)
        .distinct()
        .count();
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
