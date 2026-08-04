// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.local;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import com.yugabyte.yw.forms.SoftwareUpgradeParams;
import com.yugabyte.yw.models.Universe;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.junit.Test;

/**
 * Fault-injected upgrade and rollback sweeps that cross the PG11 -> PG15 boundary, which is the
 * slower half of the retry coverage because of the YSQL major-version catalog upgrade. Split out of
 * {@link SoftwareUpgradeRetryLocalTest} so the two halves occupy separate forks.
 */
@Slf4j
public class SoftwareUpgradeRetryPg15LocalTest extends SoftwareUpgradeRetryLocalTestBase {

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
  public void testPG15RollbackUpgradeWithRetries() throws InterruptedException {
    Universe universe = createPg11Universe();

    runUpgradeThenFaultInjectedRollback(
        universe.getUniverseUUID(), PG_15_DB_VERSION, PG_11_DB_VERSION);
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
}
