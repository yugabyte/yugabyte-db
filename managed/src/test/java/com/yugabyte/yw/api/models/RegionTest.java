// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.api.models;

import static org.hamcrest.core.StringContains.containsString;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertThat;
import static org.junit.Assert.assertTrue;

import java.util.Set;

import com.yugabyte.yw.models.AvailabilityZone;
import org.junit.Before;
import org.junit.Test;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;


public class RegionTest extends FakeDBApplication {
  Provider defaultProvider;

  @Before
  public void setUp() {
    defaultProvider = Provider.create("aws", "Amazon");
  }

  @Test
  public void testCreate() {
    Region region =
        Region.create(defaultProvider, "region-1", "Awesome PlacementRegion", "default-image");

    assertEquals(region.code, "region-1");
    assertEquals(region.name, "Awesome PlacementRegion");
    assertEquals(region.provider.name, "Amazon");
    assertTrue(region.isActive());
  }

  @Test
  public void testCreateDuplicateRegion() {
    Region.create(defaultProvider, "region-1", "region 1", "default-image");
    try {
      Region.create(defaultProvider, "region-1", "region 1", "default-image");
    } catch (Exception e) {
      assertThat(e.getMessage(), containsString("Unique index or primary key violation:"));
    }
  }

  @Test
  public void testInactiveRegion() {
    Region region = Region.create(defaultProvider, "region-1", "region 1", "default-image");

    assertNotNull(region);
    assertEquals(region.code, "region-1");
    assertEquals(region.name, "region 1");
    assertTrue(region.isActive());

    region.setActiveFlag(false);
    region.save();

    Region fetch = Region.find.byId(region.uuid);
    assertFalse(fetch.isActive());
  }

  @Test
  public void testFindRegionByProvider() {
    Region.create(defaultProvider, "region-1", "region 1", "default-image");
    Region.create(defaultProvider, "region-2", "region 2", "default-image");

    Provider provider2 = Provider.create("gce", "Google");
    Region.create(provider2, "region-3", "region 3", "default-image");

    Set<Region> regions = Region.find.where().eq("provider_uuid", defaultProvider.uuid).findSet();
    assertEquals(regions.size(), 2);
    for (Region region:regions) {
      assertThat(region.code, containsString("region-"));
    }
  }

  @Test
  public void testSettingValidLatLong() {
    Region r = Region.create(defaultProvider, "region-1", "region 1", "default-image");
    r.setLatLon(-10, 120);
    assertEquals(r.latitude, -10, 0);
    assertEquals(r.longitude, 120, 0);
  }

  @Test(expected=IllegalArgumentException.class)
  public void testSettingInvalidLatLong() {
    Region r = Region.create(defaultProvider, "region-1", "region 1", "default-image");
    r.setLatLon(-90, 200);
  }

  @Test
  public void testDisableRegionZones() {
    Region r = Region.create(defaultProvider, "region-1", "region 1", "default-image");
    AvailabilityZone.create(r, "az-1", "AZ - 1", "subnet-1");
    AvailabilityZone.create(r, "az-2", "AZ - 2", "subnet-2");

    assertTrue(r.isActive());
    for (AvailabilityZone zone : AvailabilityZone.getAZsForRegion(r.uuid)) {
      assertTrue(zone.isActive());
    }

    r.disableRegionAndZones();
    assertFalse(r.isActive());
    for (AvailabilityZone zone : AvailabilityZone.getAZsForRegion(r.uuid)) {
      assertFalse(zone.isActive());
    }
  }
}
