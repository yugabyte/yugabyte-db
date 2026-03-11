// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import java.util.Collections;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class SaveSoftwareUpgradeProgressTest extends FakeDBApplication {

  private Customer defaultCustomer;
  private Universe universe;
  private SaveSoftwareUpgradeProgress task;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    universe = ModelFactory.createUniverse("universe", defaultCustomer.getId());
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            u -> {
              UniverseDefinitionTaskParams details = u.getUniverseDetails();
              details.updateInProgress = true;
              details.prevYBSoftwareConfig =
                  new UniverseDefinitionTaskParams.PrevYBSoftwareConfig();
              details.prevYBSoftwareConfig.setSoftwareVersion("2.20.0.0-b1");
              u.setUniverseDetails(details);
            });

    task = app.injector().instanceOf(SaveSoftwareUpgradeProgress.class);
    SaveSoftwareUpgradeProgress.Params params = new SaveSoftwareUpgradeProgress.Params();
    params.setUniverseUUID(universe.getUniverseUUID());
    params.mastersUpgradeCompleted = true;
    UUID azUuid = UUID.randomUUID();
    params.primaryClusterAZsCompleted = Collections.singletonList(azUuid);
    task.initialize(params);
  }

  @Test
  public void testSaveSoftwareUpgradeProgressSetsPaused() {
    task.run();
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    assertEquals(
        UniverseDefinitionTaskParams.SoftwareUpgradeState.Paused,
        universe.getUniverseDetails().softwareUpgradeState);
    UniverseDefinitionTaskParams.PrevYBSoftwareConfig prev =
        universe.getUniverseDetails().prevYBSoftwareConfig;
    assertNotNull(prev);
    assertNotNull(prev.getPrimaryClusterAZsCompleted());
    assertEquals(1, prev.getPrimaryClusterAZsCompleted().size());
  }
}
