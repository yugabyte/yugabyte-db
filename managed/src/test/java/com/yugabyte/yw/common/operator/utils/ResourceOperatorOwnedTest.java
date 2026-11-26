// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.operator.utils;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import java.util.Set;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;

public class ResourceOperatorOwnedTest extends FakeDBApplication {

  private static final String BLOCK_KEY =
      "yb.kubernetes.operator.block_api_operator_owned_resources";

  private Customer customer;
  private Universe universe;
  private ResourceOperatorOwned resourceOperatorOwned;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    universe = ModelFactory.createUniverse(customer.getId());
    resourceOperatorOwned = app.injector().instanceOf(ResourceOperatorOwned.class);
  }

  private void markOperatorOwned(Universe u) {
    Universe.saveDetails(
        u.getUniverseUUID(),
        univ -> univ.getUniverseDetails().isKubernetesOperatorControlled = true);
  }

  @Test
  public void isUniverseBlockedOnlyForAnOperatorOwnedUniverseWhileTheBlockIsOn() {
    assertFalse(resourceOperatorOwned.isUniverseBlocked(universe.getUniverseUUID()));

    markOperatorOwned(universe);
    assertTrue(resourceOperatorOwned.isUniverseBlocked(universe.getUniverseUUID()));
    // Not a universe, for example the target of a backup task.
    assertFalse(resourceOperatorOwned.isUniverseBlocked(UUID.randomUUID()));

    mutableConfigFactory.globalRuntimeConf().setValue(BLOCK_KEY, "false");
    assertFalse(resourceOperatorOwned.isUniverseBlocked(universe.getUniverseUUID()));
  }

  @Test
  public void getBlockedUniverseUUIDsMatchesTheSingleUniverseRule() {
    Universe other = ModelFactory.createUniverse("Other Universe", customer.getId());
    Long customerId = customer.getId();
    UUID owned = universe.getUniverseUUID();
    assertTrue(resourceOperatorOwned.getBlockedUniverseUUIDs(customerId, null).isEmpty());

    markOperatorOwned(universe);
    // Only the owned universe, not the other one of the same customer.
    assertEquals(Set.of(owned), resourceOperatorOwned.getBlockedUniverseUUIDs(customerId, null));
    assertEquals(Set.of(owned), resourceOperatorOwned.getBlockedUniverseUUIDs(customerId, owned));
    assertTrue(
        resourceOperatorOwned
            .getBlockedUniverseUUIDs(customerId, other.getUniverseUUID())
            .isEmpty());
    assertTrue(
        resourceOperatorOwned.getBlockedUniverseUUIDs(customerId, UUID.randomUUID()).isEmpty());

    mutableConfigFactory.globalRuntimeConf().setValue(BLOCK_KEY, "false");
    assertTrue(resourceOperatorOwned.getBlockedUniverseUUIDs(customerId, null).isEmpty());
  }
}
