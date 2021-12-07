// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.hamcrest.core.AllOf.allOf;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThat;
import static org.junit.Assert.assertTrue;

import java.util.UUID;
import org.junit.Test;

public class AccessKeyIdTest {
  @Test
  public void testEquals() {
    UUID providerUUID = UUID.randomUUID();
    AccessKeyId id1 = AccessKeyId.create(providerUUID, "key-code-1");
    AccessKeyId id2 = AccessKeyId.create(providerUUID, "key-code-1");
    assertTrue(id1.equals(id2));
  }

  @Test
  public void testHashCode() {
    UUID providerUUID = UUID.randomUUID();
    AccessKeyId id1 = AccessKeyId.create(providerUUID, "key-code-1");
    assertEquals(id1.hashCode(), "key-code-1".hashCode() + providerUUID.hashCode());
  }

  @Test
  public void testToString() {

    UUID providerUUID = UUID.randomUUID();
    AccessKeyId id1 = AccessKeyId.create(providerUUID, "key-code-1");
    assertThat(id1.toString(), allOf(notNullValue(), equalTo("key-code-1:" + providerUUID)));
  }
}
