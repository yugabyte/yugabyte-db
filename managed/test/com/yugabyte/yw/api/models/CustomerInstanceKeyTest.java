// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.api.models;

import org.junit.BeforeClass;
import org.junit.Test;

import com.yugabyte.yw.models.CustomerInstanceKey;

import java.util.UUID;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

public class CustomerInstanceKeyTest {
  private static UUID defaultInstanceId;
  private static UUID defaultCustomerId;

  @BeforeClass
  public static void setUp() {
    defaultCustomerId = UUID.randomUUID();
    defaultInstanceId = UUID.randomUUID();
  }

  @Test
  public void testEquals() {
    CustomerInstanceKey key1 = CustomerInstanceKey.create(defaultInstanceId, defaultCustomerId);
    CustomerInstanceKey key2 = CustomerInstanceKey.create(defaultInstanceId, defaultCustomerId);

    assertTrue(key1.equals(key2));
  }

  @Test
  public void testHashCode() {
    CustomerInstanceKey key = CustomerInstanceKey.create(defaultInstanceId, defaultCustomerId);

    assertEquals(key.hashCode(), defaultInstanceId.hashCode() + defaultCustomerId.hashCode());
  }
}
