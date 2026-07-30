// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.forms;

import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.TestHelper;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import org.junit.Before;
import org.junit.Test;

public class RollbackUpgradeParamsTest extends FakeDBApplication {

  private Customer customer;
  private Universe universe;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    universe = ModelFactory.createUniverse("RollbackTestUniverse", customer.getId());
  }

  @Test
  public void testRollbackUpgradeParamsAllowsPausedState() {
    TestHelper.updateUniverseSoftwareUpgradeState(
        universe, UniverseDefinitionTaskParams.SoftwareUpgradeState.Paused);
    TestHelper.updateUniverseIsRollbackAllowed(universe, true);
    TestHelper.updateUniversePrevSoftwareConfig(
        universe, new UniverseDefinitionTaskParams.PrevYBSoftwareConfig());

    RollbackUpgradeParams params = new RollbackUpgradeParams();
    params.upgradeOption = UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE;
    params.clusters.add(universe.getUniverseDetails().getPrimaryCluster());

    Universe u = Universe.getOrBadRequest(universe.getUniverseUUID());
    params.verifyParams(u, true);
    // no exception - Paused is in ALLOWED_UNIVERSE_ROLLBACK_UPGRADE_STATE_SET
  }

  @Test
  public void testRollbackUpgradeParamsRejectsPausedStateWhenRollbackNotAllowed() {
    TestHelper.updateUniverseSoftwareUpgradeState(
        universe, UniverseDefinitionTaskParams.SoftwareUpgradeState.Paused);
    TestHelper.updateUniverseIsRollbackAllowed(universe, false);

    RollbackUpgradeParams params = new RollbackUpgradeParams();
    params.upgradeOption = UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE;
    params.clusters.add(universe.getUniverseDetails().getPrimaryCluster());

    Universe u = Universe.getOrBadRequest(universe.getUniverseUUID());
    PlatformServiceException ex =
        assertThrows(PlatformServiceException.class, () -> params.verifyParams(u, true));
    assertTrue(ex.getMessage().contains("finalized"));
  }
}
