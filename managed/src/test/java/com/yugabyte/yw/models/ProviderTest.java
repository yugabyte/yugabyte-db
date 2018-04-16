// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.models;

import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.commissioner.Common;
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
    Provider provider = ModelFactory.awsProvider(defaultCustomer);

    assertNotNull(provider.uuid);
    assertEquals(provider.name, "Amazon");
    assertTrue(provider.isActive());
  }

  @Test
  public void testNullConfig() {
    Provider provider = ModelFactory.awsProvider(defaultCustomer);
    assertNotNull(provider.uuid);
    assertTrue(provider.getConfig().isEmpty());
  }

  @Test
  public void testNotNullConfig() {
    Provider provider = Provider.create(defaultCustomer.uuid, Common.CloudType.aws,
        "Amazon", ImmutableMap.of("Foo", "Bar"));
    assertNotNull(provider.uuid);
    assertNotNull(provider.getConfig().toString(), allOf(notNullValue(), equalTo("{Foo=Bar}")));
  }

  @Test
  public void testCreateDuplicateProvider() {
    ModelFactory.awsProvider(defaultCustomer);
    try {
      Provider.create(defaultCustomer.uuid, Common.CloudType.aws, "Amazon");
    } catch (Exception e) {
      assertThat(e.getMessage(), containsString("Unique index or primary key violation:"));
    }
  }

  @Test
  public void testGetMaskedConfig() {
    Provider provider = Provider.create(defaultCustomer.uuid, Common.CloudType.aws,
            "Amazon", ImmutableMap.of("AWS_ACCESS_KEY_ID", "BarBarBarBar"));
    assertNotNull(provider.uuid);
    assertNotNull(provider.getConfig().toString(), allOf(notNullValue(), equalTo("{AWS_ACCESS_KEY_ID=Ba********ar}")));
  }

  @Test
  public void testCreateProviderWithSameName() {
    Provider p1 = ModelFactory.awsProvider(defaultCustomer);
    Provider p2 = Provider.create(UUID.randomUUID(), Common.CloudType.aws, "Amazon");
    assertNotNull(p1);
    assertNotNull(p2);
  }

  @Test
  public void testInactiveProvider() {
    Provider provider = ModelFactory.awsProvider(defaultCustomer);

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
    Provider provider = ModelFactory.awsProvider(defaultCustomer);

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
    Provider provider = ModelFactory.awsProvider(defaultCustomer);
    Provider fetch = Provider.get(defaultCustomer.uuid, Common.CloudType.aws);
    assertNotNull(fetch);
    assertEquals(fetch.uuid, provider.uuid);
    assertEquals(fetch.name, provider.name);
    assertTrue(fetch.isActive());
    assertEquals(fetch.customerUUID, defaultCustomer.uuid);
  }

  @Test
  public void testGetByNameFailure() {
    Provider.create(defaultCustomer.uuid, Common.CloudType.aws, "Amazon");
    Provider.create(defaultCustomer.uuid, Common.CloudType.gcp, "Amazon");
    try {
      Provider.get(defaultCustomer.uuid, Common.CloudType.aws);
    } catch (RuntimeException re) {
      assertThat(re.getMessage(), allOf(notNullValue(),
              equalTo("Found 2 providers with name: Amazon")));
    }
  }

  @Test
  public void testCascadeDelete() {
    Provider provider = ModelFactory.awsProvider(defaultCustomer);
    Region region = Region.create(provider, "region-1", "region 1", "ybImage");
    AvailabilityZone.create(region, "zone-1", "zone 1", "subnet-1");
    provider.delete();
    assertEquals(0, Region.find.all().size());
    assertEquals(0, AvailabilityZone.find.all().size());
  }
}
