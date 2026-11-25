// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.controllers;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.forms.RestartTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import org.junit.Before;
import org.junit.Test;

public class UniverseControllerRequestBinderMergeTest extends FakeDBApplication {

  private Universe universe;

  @Before
  public void setUp() {
    Customer customer = ModelFactory.testCustomer();
    universe = ModelFactory.createUniverse(customer.getId());
    // Older clients sent 0 for both waits on create, so the universe stores 0.
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            u -> {
              u.getUniverseDetails().sleepAfterMasterRestartMillis = 0;
              u.getUniverseDetails().sleepAfterTServerRestartMillis = 0;
            });
  }

  @Test
  public void testOmittedRestartWaitsIgnoreStoredValues() {
    RestartTaskParams merged =
        UniverseControllerRequestBinder.mergeWithUniverse(
            new RestartTaskParams(), universe, RestartTaskParams.class);
    assertNull(merged.sleepAfterMasterRestartMillis);
    assertNull(merged.sleepAfterTServerRestartMillis);
  }

  @Test
  public void testRequestRestartWaitsAreKept() {
    RestartTaskParams params = new RestartTaskParams();
    params.sleepAfterMasterRestartMillis = 500;
    params.sleepAfterTServerRestartMillis = 0;
    RestartTaskParams merged =
        UniverseControllerRequestBinder.mergeWithUniverse(
            params, universe, RestartTaskParams.class);
    assertEquals(Integer.valueOf(500), merged.sleepAfterMasterRestartMillis);
    assertEquals(Integer.valueOf(0), merged.sleepAfterTServerRestartMillis);
  }
}
