// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.models;

import com.yugabyte.yw.models.InstanceTypeKey;
import org.junit.Test;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

public class InstanceTypeKeyTest {

  @Test
  public void testEquals() {
    InstanceTypeKey key1 = InstanceTypeKey.create("it-1", "aws");
    InstanceTypeKey key2 = InstanceTypeKey.create("it-1", "aws");
    assertTrue(key1.equals(key2));
  }

  @Test
  public void testHashCode() {
    InstanceTypeKey key = InstanceTypeKey.create("it-1", "aws");
    assertEquals(key.hashCode(), "it-1".hashCode() + "aws".hashCode());
  }
}
