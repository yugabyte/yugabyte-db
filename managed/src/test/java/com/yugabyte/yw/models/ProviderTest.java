// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.models;

import com.yugabyte.yw.common.ModelFactory;
import org.junit.Before;
import org.junit.Test;

import com.yugabyte.yw.common.FakeDBApplication;

import java.util.UUID;

import static org.hamcrest.CoreMatchers.*;
import static org.junit.Assert.*;

public class ProviderTest extends FakeDBApplication {
  private Customer defaultCustomer;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
  }

  @Test
  public void testCreate() {
    Provider provider = Provider.create(defaultCustomer.uuid, "aws", "Amazon");

    assertNotNull(provider.uuid);
    assertEquals(provider.name, "Amazon");
    assertTrue(provider.isActive());
  }

  @Test
  public void testCreateDuplicateProvider() {
    Provider.create(defaultCustomer.uuid, "aws", "Amazon");
    try {
      Provider.create(defaultCustomer.uuid, "aws", "Amazon");
    } catch (Exception e) {
      assertThat(e.getMessage(), containsString("Unique index or primary key violation:"));
    }
  }

  @Test
  public void testCreateProviderWithSameName() {
    Provider p1 = Provider.create(defaultCustomer.uuid, "aws", "Amazon");
    Provider p2 = Provider.create(UUID.randomUUID(), "aws", "Amazon");
    assertNotNull(p1);
    assertNotNull(p2);
  }

  @Test
  public void testInactiveProvider() {
    Provider provider = Provider.create(defaultCustomer.uuid, "aws", "Amazon");

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
    Provider provider = Provider.create(defaultCustomer.uuid, "aws", "Amazon");

    assertNotNull(provider.uuid);
    Provider fetch = Provider.find.byId(provider.uuid);
    assertNotNull(fetch);
    assertEquals(fetch.uuid, provider.uuid);
    assertEquals(fetch.name, provider.name);
    assertTrue(fetch.isActive());
    assertEquals(fetch.customerUUID, defaultCustomer.uuid);
  }

  @Test
  public void testGetByNameSuccess() {
    Provider provider = Provider.create(defaultCustomer.uuid, "aws", "Amazon");
    Provider fetch = Provider.get(defaultCustomer.uuid, "Amazon");
    assertNotNull(fetch);
    assertEquals(fetch.uuid, provider.uuid);
    assertEquals(fetch.name, provider.name);
    assertTrue(fetch.isActive());
    assertEquals(fetch.customerUUID, defaultCustomer.uuid);
  }

  @Test
  public void testGetByNameFailure() {
    Provider.create(defaultCustomer.uuid, "aws-1", "Amazon");
    Provider.create(defaultCustomer.uuid, "aws-2", "Amazon");
    try {
      Provider.get(defaultCustomer.uuid, "Amazon");
    } catch (RuntimeException re) {
      assertThat(re.getMessage(), allOf(notNullValue(),
              equalTo("Found 2 providers with name: Amazon")));
    }
  }
}
