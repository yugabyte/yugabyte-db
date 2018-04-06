// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import static org.junit.Assert.*;

import java.util.Date;
import java.util.List;
import java.util.UUID;

import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.HealthCheck;

import org.junit.Before;
import org.junit.Test;

import com.yugabyte.yw.common.FakeDBApplication;

public class HealthCheckTest extends FakeDBApplication {
  private Customer defaultCustomer;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
  }

  private void addChecks(UUID universeUUID, int numChecks) {
    for (int i = 0; i < numChecks; ++i) {
      try {
        // The checkTime is created internally and part of the primary key
        Thread.sleep(10);
      } catch(InterruptedException e) {
        // Ignore in test..
      }
      HealthCheck check = HealthCheck.addAndPrune(
          universeUUID, defaultCustomer.getCustomerId(), "{}");
      assertNotNull(check);
    }
    List<HealthCheck> checks = HealthCheck.getAll(universeUUID);
    assertNotNull(checks);
    // If we were asked to insert over the limit, pruning will happen.
    assertEquals(checks.size(), Math.min(HealthCheck.RECORD_LIMIT, checks.size()));
  }

  @Test
  public void testAddToLimit() {
    UUID universeUUID = UUID.randomUUID();
    addChecks(universeUUID, HealthCheck.RECORD_LIMIT);
  }

  @Test
  public void testPruneEnoughItems() {
    UUID universeUUID = UUID.randomUUID();
    addChecks(universeUUID, 2 * HealthCheck.RECORD_LIMIT);
  }

  @Test
  public void testPruneRightItems() throws java.lang.InterruptedException {
    UUID universeUUID = UUID.randomUUID();
    addChecks(universeUUID, HealthCheck.RECORD_LIMIT);
    Date now = new Date();
    addChecks(universeUUID, HealthCheck.RECORD_LIMIT);
    List<HealthCheck> after = HealthCheck.getAll(universeUUID);
    for (HealthCheck check : after) {
      assertTrue(now.compareTo(check.idKey.checkTime) < 0);
    }
  }
}
