// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.rollback;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.SoftwareUpgradeState;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class SoftwareUpgradeRollbackComputerTest extends FakeDBApplication {

  private SoftwareUpgradeRollbackComputer computer;
  private Universe universe;
  private TaskInfo failedUpgrade;

  @Before
  public void setUp() {
    computer = app.injector().instanceOf(SoftwareUpgradeRollbackComputer.class);
    universe = ModelFactory.createUniverse(ModelFactory.testCustomer().getId());
    failedUpgrade = new TaskInfo(TaskType.SoftwareUpgradeYB, null);
    failedUpgrade.setUuid(UUID.randomUUID());
  }

  private boolean canRollbackWith(UUID owner, SoftwareUpgradeState state, boolean rollbackAllowed) {
    Universe.saveDetails(
        universe.getUniverseUUID(),
        u -> {
          UniverseDefinitionTaskParams details = u.getUniverseDetails();
          details.updatingTaskUUID = owner;
          details.placementModificationTaskUuid = owner;
          details.softwareUpgradeState = state;
          details.isSoftwareRollbackAllowed = rollbackAllowed;
          u.setUniverseDetails(details);
        });
    return computer.canRollback(failedUpgrade, universe.getUniverseUUID());
  }

  @Test
  public void testAllowedAfterUpgradeStarted() {
    assertTrue(canRollbackWith(failedUpgrade.getUuid(), SoftwareUpgradeState.UpgradeFailed, true));
  }

  @Test
  public void testRejectedBeforeUpgradeStarted() {
    // The Upgrading step never ran, so the state and flag still describe the previous upgrade.
    assertFalse(canRollbackWith(failedUpgrade.getUuid(), SoftwareUpgradeState.Ready, false));
    assertFalse(canRollbackWith(failedUpgrade.getUuid(), SoftwareUpgradeState.Ready, true));
  }

  @Test
  public void testRejectedWhenRollbackNotAllowed() {
    assertFalse(
        canRollbackWith(failedUpgrade.getUuid(), SoftwareUpgradeState.UpgradeFailed, false));
  }

  @Test
  public void testRejectedWhenTaskDoesNotOwnUniverse() {
    assertFalse(canRollbackWith(UUID.randomUUID(), SoftwareUpgradeState.UpgradeFailed, true));
    assertFalse(canRollbackWith(null, SoftwareUpgradeState.UpgradeFailed, true));
  }

  @Test
  public void testRejectedWhenUniverseMissing() {
    assertFalse(computer.canRollback(failedUpgrade, UUID.randomUUID()));
  }
}
