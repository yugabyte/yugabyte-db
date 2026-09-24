// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.models;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.forms.SupportBundleFormDataV2;
import com.yugabyte.yw.models.helpers.BundleDetails.ComponentType;
import java.util.Date;
import java.util.EnumSet;
import org.junit.Before;
import org.junit.Test;

public class SupportBundleV2Test extends FakeDBApplication {
  private Customer customer;
  private Universe universe;
  private RuntimeConfGetter confGetter;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    universe = ModelFactory.createUniverse(customer.getId());
    confGetter = app.injector().instanceOf(RuntimeConfGetter.class);
  }

  @Test
  public void testCreateAndGetV2Bundle() {
    SupportBundleFormDataV2 bundleData = new SupportBundleFormDataV2();
    bundleData.startDate = new Date();
    bundleData.endDate = new Date();
    bundleData.components = EnumSet.of(ComponentType.YBAComponent);

    SupportBundleV2 created = SupportBundleV2.create(bundleData, universe, confGetter);
    assertNotNull(created.getBundleUUID());
    assertEquals(SupportBundleV2StatusType.Running, created.getStatus());
    assertEquals(universe.getUniverseUUID(), created.getScopeUUID());
    assertNotNull(created.getBundleDetails());
    assertTrue(created.getBundleDetails().getComponents().contains(ComponentType.YBAComponent));
  }
}
