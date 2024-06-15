// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.models;

import static org.hamcrest.CoreMatchers.allOf;
import static org.hamcrest.CoreMatchers.containsString;
import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertThat;
import static org.junit.Assert.assertTrue;

import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import java.util.Map;
import java.util.Set;
import org.junit.Before;
import org.junit.Test;

public class AvailabilityZoneTest extends FakeDBApplication {
  Region defaultRegion;
  Provider provider;

  @Before
  public void setUp() {
    Customer customer = ModelFactory.testCustomer();
    provider = ModelFactory.awsProvider(customer);
    defaultRegion = Region.create(provider, "region-1", "test region", "default-image");
  }

  @Test
  public void testCreate() {
    AvailabilityZone az =
        AvailabilityZone.createOrThrow(defaultRegion, "az-1", "A Zone", "subnet-1");
    assertEquals(az.getCode(), "az-1");
    assertEquals(az.getName(), "A Zone");
    assertEquals(az.getRegion().getCode(), "region-1");
    assertTrue(az.isActive());
  }

  @Test
  public void testCreateDuplicateAZ() {
    AvailabilityZone.createOrThrow(defaultRegion, "az-1", "A Zone", "subnet-1");
    try {
      AvailabilityZone.createOrThrow(defaultRegion, "az-1", "A Zone 2", "subnet-2");
    } catch (Exception e) {
      assertThat(e.getMessage(), containsString("Unique index or primary key violation:"));
    }
  }

  @Test
  public void testInactiveAZ() {
    AvailabilityZone az =
        AvailabilityZone.createOrThrow(defaultRegion, "az-1", "A Zone", "subnet-1");

    assertEquals(az.getCode(), "az-1");
    assertEquals(az.getName(), "A Zone");
    assertEquals(az.getRegion().getCode(), "region-1");
    assertTrue(az.isActive());

    az.setActive(false);
    az.save();

    AvailabilityZone fetch = AvailabilityZone.find.byId(az.getUuid());
    assertFalse(fetch.isActive());
  }

  @Test
  public void testFindAZByRegion() {
    AvailabilityZone.createOrThrow(defaultRegion, "az-1", "A Zone 1", "subnet-1");
    AvailabilityZone.createOrThrow(defaultRegion, "az-2", "A Zone 2", "subnet-2");

    Set<AvailabilityZone> zones =
        AvailabilityZone.find.query().where().eq("region_uuid", defaultRegion.getUuid()).findSet();
    assertEquals(zones.size(), 2);
    for (AvailabilityZone zone : zones) {
      assertThat(zone.getCode(), containsString("az-"));
    }
  }

  @Test
  public void testGetProvider() {
    AvailabilityZone az =
        AvailabilityZone.createOrThrow(defaultRegion, "az-1", "A Zone", "subnet-1");
    Provider p = az.getProvider();
    assertNotNull(p);
    assertEquals(p, provider);
  }

  @Test
  public void testNullConfig() {
    AvailabilityZone az =
        AvailabilityZone.createOrThrow(defaultRegion, "az-1", "A Zone", "subnet-1");
    assertNotNull(az.getUuid());
    Map<String, String> envVars = CloudInfoInterface.fetchEnvVars(az);
    assertTrue(envVars.isEmpty());
  }

  @Test
  public void testNotNullConfig() {
    AvailabilityZone az =
        AvailabilityZone.createOrThrow(defaultRegion, "az-1", "A Zone", "subnet-1");
    az.updateConfig(ImmutableMap.of("Foo", "Bar"));
    az.save();
    Map<String, String> envVars = CloudInfoInterface.fetchEnvVars(az);
    assertNotNull(az.getUuid());
    assertNotNull(envVars.toString(), allOf(notNullValue(), equalTo("{Foo=Bar}")));
  }

  @Test
  public void testAzCodeLength() {
    String azCode = String.valueOf('A').repeat(110);
    try {
      AvailabilityZone az =
          AvailabilityZone.createOrThrow(defaultRegion, azCode, "A Zone", "subnet-1");
    } catch (Exception e) {
      assertTrue(e.getMessage().contains("Unable to create zone: " + azCode));
    }
  }
}
