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

  private boolean canRollbackWith(UUID owner, SoftwareUpgradeState state) {
    Universe.saveDetails(
        universe.getUniverseUUID(),
        u -> {
          UniverseDefinitionTaskParams details = u.getUniverseDetails();
          details.updatingTaskUUID = owner;
          details.placementModificationTaskUuid = owner;
          details.softwareUpgradeState = state;
          u.setUniverseDetails(details);
        });
    return computer.canRollback(failedUpgrade, universe.getUniverseUUID());
  }

  @Test
  public void testAllowedWhenTaskOwnsUniverse() {
    // After the freeze the rollback's own precheck decides, whatever state the task left.
    for (SoftwareUpgradeState state :
        new SoftwareUpgradeState[] {
          SoftwareUpgradeState.UpgradeFailed,
          SoftwareUpgradeState.PreFinalize,
          SoftwareUpgradeState.Ready
        }) {
      assertTrue(state.name(), canRollbackWith(failedUpgrade.getUuid(), state));
    }
  }

  @Test
  public void testRejectedWhenTaskDoesNotOwnUniverse() {
    // A task that failed before the freeze never took ownership.
    assertFalse(canRollbackWith(UUID.randomUUID(), SoftwareUpgradeState.Ready));
    assertFalse(canRollbackWith(null, SoftwareUpgradeState.Ready));
  }

  @Test
  public void testRejectedWhenUniverseMissing() {
    assertFalse(computer.canRollback(failedUpgrade, UUID.randomUUID()));
  }
}
