// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.models;

import static org.hamcrest.CoreMatchers.containsString;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertThat;
import static org.junit.Assert.assertTrue;

import java.util.Set;

import org.junit.Before;
import org.junit.Test;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;


public class AvailabilityZoneTest extends FakeDBApplication {
  Region defaultRegion;

  @Before
  public void setUp() {
    Provider provider = Provider.create("aws", "Amazon");
    defaultRegion = Region.create(provider, "region-1", "test region", "default-image");
  }

  @Test
  public void testCreate() {
    AvailabilityZone az = AvailabilityZone.create(defaultRegion, "az-1", "A Zone", "subnet-1");
    assertEquals(az.code, "az-1");
    assertEquals(az.name, "A Zone");
    assertEquals(az.region.code, "region-1");
    assertTrue(az.isActive());
  }

  @Test
  public void testCreateDuplicateAZ() {
    AvailabilityZone.create(defaultRegion, "az-1", "A Zone", "subnet-1");
    try {
      AvailabilityZone.create(defaultRegion, "az-1", "A Zone 2", "subnet-2");
    } catch (Exception e) {
      assertThat(e.getMessage(), containsString("Unique index or primary key violation:"));
    }
  }

  @Test
  public void testInactiveAZ() {
    AvailabilityZone az = AvailabilityZone.create(defaultRegion, "az-1", "A Zone", "subnet-1");

    assertEquals(az.code, "az-1");
    assertEquals(az.name, "A Zone");
    assertEquals(az.region.code, "region-1");
    assertTrue(az.isActive());

    az.setActiveFlag(false);
    az.save();

    AvailabilityZone fetch = AvailabilityZone.find.byId(az.uuid);
    assertFalse(fetch.isActive());
  }

  @Test
  public void testFindAZByRegion() {
    AvailabilityZone.create(defaultRegion, "az-1", "A Zone 1", "subnet-1");
    AvailabilityZone.create(defaultRegion, "az-2", "A Zone 2", "subnet-2");

    Set<AvailabilityZone> zones = AvailabilityZone.find.where().eq("region_uuid", defaultRegion.uuid).findSet();
    assertEquals(zones.size(), 2);
    for (AvailabilityZone zone : zones) {
      assertThat(zone.code, containsString("az-"));
    }
  }
}
