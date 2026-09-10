// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import com.fasterxml.jackson.databind.node.JsonNodeFactory;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.StateTransitionDetails;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class MarkRollbackUnsafeTest extends FakeDBApplication {

  private Customer defaultCustomer;
  private Universe universe;
  private MarkRollbackUnsafe task;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    universe = ModelFactory.createUniverse("universe", defaultCustomer.getId());
    task = app.injector().instanceOf(MarkRollbackUnsafe.class);
  }

  @Test
  public void testNoOpWhenDetailsNull() {
    MarkRollbackUnsafe.Params params = new MarkRollbackUnsafe.Params();
    params.setUniverseUUID(universe.getUniverseUUID());
    task.initialize(params);

    task.run();

    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    assertNull(universe.getStateTransitionDetails());
  }

  @Test
  public void testFlipsRollbackSafeAndIsIdempotent() {
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            u -> {
              u.getUniverseDetails().updateInProgress = true;
              u.setStateTransitionDetails(
                  new StateTransitionDetails(true, JsonNodeFactory.instance.objectNode()));
            });

    MarkRollbackUnsafe.Params params = new MarkRollbackUnsafe.Params();
    params.setUniverseUUID(universe.getUniverseUUID());
    task.initialize(params);

    task.run();
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    assertNotNull(universe.getStateTransitionDetails());
    assertFalse(universe.getStateTransitionDetails().isRollbackSafe());

    task.run();
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    assertNotNull(universe.getStateTransitionDetails());
    assertFalse(universe.getStateTransitionDetails().isRollbackSafe());
    assertTrue(universe.getStateTransitionDetails().getDelta().isObject());
  }
}
