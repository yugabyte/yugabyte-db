// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.models;

import org.junit.Test;

import java.util.UUID;

import static org.junit.Assert.assertEquals;

public class InstanceTypeKeyTest {

  @Test
  public void testEquals() {
    UUID providerUuid = UUID.randomUUID();
    InstanceTypeKey key1 = InstanceTypeKey.create("it-1", providerUuid);
    InstanceTypeKey key2 = InstanceTypeKey.create("it-1", providerUuid);
    assertEquals(key1, key2);
  }

  @Test
  public void testHashCode() {
    UUID providerUuid = UUID.randomUUID();
    InstanceTypeKey key = InstanceTypeKey.create("it-1", providerUuid);
    assertEquals(key.hashCode(), providerUuid.hashCode() + "it-1".hashCode());
  }
}
