// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.local;

import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import com.yugabyte.yw.forms.FinalizeUpgradeParams;
import com.yugabyte.yw.forms.SoftwareUpgradeParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.SoftwareUpgradeState;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.junit.Test;

/**
 * Fault-injected upgrade, rollback and finalize sweeps against the PG11-era release. The PG15
 * variants live in {@link SoftwareUpgradeRetryPg15LocalTest} so the two halves occupy separate
 * forks.
 */
@Slf4j
public class SoftwareUpgradeRetryLocalTest extends SoftwareUpgradeRetryLocalTestBase {

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
  public void testRollbackUpgradeWithRetries() throws InterruptedException {
    Universe universe = createRollbackCapableUniverse();
    runUpgradeThenFaultInjectedRollback(
        universe.getUniverseUUID(), PG_11_DB_VERSION, OLD_VERSION_WITH_ROLLBACK);
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
}
