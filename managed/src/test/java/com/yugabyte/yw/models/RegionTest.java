// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.models;

import static com.yugabyte.yw.common.AssertHelper.assertValue;
import static org.hamcrest.CoreMatchers.allOf;
import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.hamcrest.core.StringContains.containsString;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThat;
import static org.junit.Assert.assertTrue;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.ProviderAndRegion;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import javax.persistence.PersistenceException;
import org.hamcrest.Matchers;
import org.junit.Before;
import org.junit.Test;
import play.libs.Json;

public class RegionTest extends FakeDBApplication {
  Provider defaultProvider;
  Provider otherProvider;
  Customer defaultCustomer;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    defaultProvider = ModelFactory.awsProvider(defaultCustomer);
    otherProvider = ModelFactory.azuProvider(defaultCustomer);
  }

  @Test
  public void testCreate() {
    Region region =
        Region.create(defaultProvider, "region-1", "Awesome PlacementRegion", "default-image");

    assertEquals(region.getCode(), "region-1");
    assertEquals(region.getName(), "Awesome PlacementRegion");
    assertEquals(region.getProvider().getName(), "Amazon");
    assertTrue(region.isActive());
  }

  @Test
  public void testCreateWithLocation() {
    Region region =
        Region.create(
            defaultProvider, "region-1", "Awesome PlacementRegion", "default-image", 100, 100);
    assertEquals(region.getCode(), "region-1");
    assertEquals(region.getName(), "Awesome PlacementRegion");
    assertEquals(region.getProvider().getName(), "Amazon");
    assertEquals(region.getLatitude(), 100, 0);
    assertEquals(region.getLongitude(), 100, 0);
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
    assertEquals(region.getCode(), "region-1");
    assertEquals(region.getName(), "region 1");
    assertTrue(region.isActive());

    region.setActive(false);
    region.save();

    Region fetch = Region.find.byId(region.getUuid());
    assertFalse(fetch.isActive());
  }

  @Test
  public void testFindRegionByProvider() {
    Region.create(defaultProvider, "region-1", "region 1", "default-image");
    Region.create(defaultProvider, "region-2", "region 2", "default-image");

    Provider provider2 = ModelFactory.gcpProvider(defaultCustomer);
    Region.create(provider2, "region-3", "region 3", "default-image");

    Set<Region> regions =
        Region.find.query().where().eq("provider_uuid", defaultProvider.getUuid()).findSet();
    assertEquals(regions.size(), 2);
    for (Region region : regions) {
      assertThat(region.getCode(), containsString("region-"));
    }
  }

  @Test
  public void testFindRegionByKey() {
    Region region1 = Region.create(defaultProvider, "region-1", "region 1", "default-image");
    Region region2 = Region.create(defaultProvider, "region-2", "region 2", "default-image");
    Region region3 = Region.create(otherProvider, "region-1", "region 2", "default-image");

    List<Region> regions =
        Region.findByKeys(
            ImmutableList.of(
                new ProviderAndRegion(defaultProvider.getUuid(), "region-1"),
                new ProviderAndRegion(otherProvider.getUuid(), "region-1")));

    assertThat(regions, Matchers.containsInAnyOrder(region1, region3));
  }

  @Test
  public void testDisableRegionZones() {
    Region r = Region.create(defaultProvider, "region-1", "region 1", "default-image");
    AvailabilityZone.createOrThrow(r, "az-1", "AZ - 1", "subnet-1");
    AvailabilityZone.createOrThrow(r, "az-2", "AZ - 2", "subnet-2");

    assertTrue(r.isActive());
    for (AvailabilityZone zone : AvailabilityZone.getAZsForRegion(r.getUuid())) {
      assertTrue(zone.isActive());
    }

    r.disableRegionAndZones();
    assertFalse(r.isActive());
    for (AvailabilityZone zone : AvailabilityZone.getAZsForRegion(r.getUuid())) {
      assertFalse(zone.isActive());
    }
  }

  @Test
  public void testCreateWithValidMetadata() {
    ObjectNode metaData = Json.newObject();
    metaData.put("name", "sample region");
    metaData.put("latitude", 36.778261);
    metaData.put("longitude", -119.417932);
    metaData.put("ybImage", "yb-image-1");
    Region r = Region.createWithMetadata(defaultProvider, "region-1", metaData);
    assertNotNull(r);
    JsonNode regionJson = Json.toJson(r);
    JsonNode details = regionJson.get("details");
    JsonNode cloudInfo = details.get("cloudInfo");
    JsonNode awsRegionCloudInfo = cloudInfo.get("aws");

    assertValue(regionJson, "code", "region-1");
    assertValue(regionJson, "name", "sample region");
    assertValue(regionJson, "latitude", "36.778261");
    assertValue(regionJson, "longitude", "-119.417932");
    assertValue(awsRegionCloudInfo, "ybImage", "yb-image-1");
  }

  @Test(expected = PersistenceException.class)
  public void testCreateWithEmptyMetadata() {
    Region.createWithMetadata(defaultProvider, "region-1", Json.newObject());
  }

  @Test
  public void testGetWithValidUUIDs() {
    Region r = Region.create(defaultProvider, "region-1", "region 1", "default-image");
    Region fetchedRegion =
        Region.get(defaultCustomer.getUuid(), defaultProvider.getUuid(), r.getUuid());
    assertEquals(r, fetchedRegion);
  }

  @Test
  public void testGetWithInvalidCustomerUUID() {
    Region r = Region.create(defaultProvider, "region-1", "region 1", "default-image");
    UUID randomUUID = UUID.randomUUID();
    Region fetchedRegion = Region.get(randomUUID, defaultProvider.getUuid(), r.getUuid());
    assertNull(fetchedRegion);
  }

  @Test
  public void testGetWithInvalidProviderUUID() {
    Region r = Region.create(defaultProvider, "region-1", "region 1", "default-image");
    UUID randomUUID = UUID.randomUUID();
    Region fetchedRegion = Region.get(defaultCustomer.getUuid(), randomUUID, r.getUuid());
    assertNull(fetchedRegion);
  }

  @Test
  public void testCascadeDelete() {
    Region r = Region.create(defaultProvider, "region-1", "region 1", "default-image");
    AvailabilityZone.createOrThrow(r, "az-1", "az 1", "subnet-1");
    r.delete();
    assertEquals(0, AvailabilityZone.find.all().size());
  }

  @Test
  public void testGetByProviderMultipleProviders() {
    Provider testProvider = ModelFactory.gcpProvider(defaultCustomer);
    Region r = Region.create(defaultProvider, "region-1", "region 1", "default-image");
    Region r1 = Region.create(testProvider, "region-2", "region 2", "default-image");
    List<Region> fetchedRegions = Region.getByProvider(defaultProvider.getUuid());
    assertEquals(fetchedRegions.size(), 1);
  }

  @Test
  public void testGetByProviderMultipleRegions() {
    Region r = Region.create(defaultProvider, "region-1", "region 1", "default-image");
    Region r1 = Region.create(defaultProvider, "region-2", "region 2", "default-image");
    List<Region> fetchedRegions = Region.getByProvider(defaultProvider.getUuid());
    assertEquals(fetchedRegions.size(), 2);
  }

  @Test
  public void testNullConfig() {
    Region r = Region.create(defaultProvider, "region-1", "region 1", "default-image");
    assertNotNull(r.getUuid());
    Map<String, String> envVars = CloudInfoInterface.fetchEnvVars(r);
    assertEquals("{ybImage=default-image}", envVars.toString());
  }

  @Test
  public void testNotNullConfig() {
    Region r = Region.create(defaultProvider, "region-1", "region 1", "default-image");
    r.setConfig(ImmutableMap.of("Foo", "Bar"));
    r.save();
    Map<String, String> envVars = CloudInfoInterface.fetchEnvVars(r);
    assertNotNull(r.getUuid());
    assertNotNull(envVars.toString(), allOf(notNullValue(), equalTo("{Foo=Bar}")));
  }
}
