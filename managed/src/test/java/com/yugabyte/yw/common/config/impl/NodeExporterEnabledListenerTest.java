// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.config.impl;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;

public class NodeExporterEnabledListenerTest extends FakeDBApplication {

  private final NodeExporterEnabledListener listener = new NodeExporterEnabledListener();
  private Customer customer;
  private Universe k8s;
  private Universe vm;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    k8s =
        ModelFactory.createK8sUniverseCustomCores(
            "k8s", UUID.randomUUID(), customer.getId(), null, null, false, 1);
    vm = ModelFactory.createUniverse(customer.getId());
  }

  @Test
  public void testUniverseScopeFlagsTargetRewrite() {
    listener.processUniverse(k8s);
    assertFalse(swamperConfigWritten(k8s));
  }

  @Test
  public void testGlobalAndCustomerScopesFlagOnlyKubernetes() {
    listener.processGlobal();
    assertFalse(swamperConfigWritten(k8s));
    assertTrue(swamperConfigWritten(vm));

    k8s.setSwamperConfigWritten(true);
    k8s.save();
    listener.processCustomer(customer);
    assertFalse(swamperConfigWritten(k8s));
    assertTrue(swamperConfigWritten(vm));
  }

  private boolean swamperConfigWritten(Universe universe) {
    return Universe.getOrBadRequest(universe.getUniverseUUID()).getSwamperConfigWritten();
  }
}
