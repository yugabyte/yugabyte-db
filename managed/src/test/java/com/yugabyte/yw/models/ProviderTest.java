// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.models;

import org.junit.Test;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.models.Provider;

import static org.hamcrest.CoreMatchers.*;
import static org.junit.Assert.*;

public class ProviderTest extends FakeDBApplication {
  @Test
  public void testCreate() {
    Provider provider = Provider.create("aws", "Amazon");

    assertNotNull(provider.uuid);
    assertEquals(provider.name, "Amazon");
    assertTrue(provider.isActive());
  }

  @Test
  public void testCreateDuplicateProvider() {
    Provider.create("aws", "Amazon");
    try {
      Provider.create("aws", "Amazon");
    } catch (Exception e) {
      assertThat(e.getMessage(), containsString("Unique index or primary key violation:"));
    }
  }

  @Test
  public void testInactiveProvider() {
    Provider provider = Provider.create("aws", "Amazon");

    assertNotNull(provider.uuid);
    assertEquals(provider.name, "Amazon");
    assertTrue(provider.isActive());

    provider.setActiveFlag(false);
    provider.save();

    Provider fetch = Provider.find.byId(provider.uuid);
    assertFalse(fetch.isActive());
  }

  @Test
  public void testFindProvider() {
    Provider provider = Provider.create("aws", "Amazon");

    assertNotNull(provider.uuid);
    Provider fetch = Provider.find.byId(provider.uuid);
    assertNotNull(fetch);
    assertEquals(fetch.uuid, provider.uuid);
    assertEquals(fetch.name, provider.name);
    assertTrue(fetch.isActive());
  }
}
