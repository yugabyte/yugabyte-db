// Copyright (c) Yugabyte, Inc.

package models.cloud;

import helpers.FakeDBApplication;
import org.junit.Before;
import org.junit.Test;

import java.util.Set;

import static org.hamcrest.CoreMatchers.containsString;
import static org.junit.Assert.*;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;


public class RegionTest extends FakeDBApplication {
	Provider defaultProvider;

	@Before
	public void setUp() {
		defaultProvider = Provider.create("Amazon");
	}

	@Test
	public void testCreate() {
		Region region = Region.create(defaultProvider, "region-1", "Awesome Region", true);

		assertEquals(region.code, "region-1");
		assertEquals(region.name, "Awesome Region");
		assertEquals(region.provider.name, "Amazon");
		assertTrue(region.isActive());
	}

	@Test
	public void testCreateDuplicateRegion() {
		Region.create(defaultProvider, "region-1", "region 1", true);
		try {
			Region.create(defaultProvider, "region-1", "region 1", true);
		} catch (Exception e) {
			assertThat(e.getMessage(), containsString("Unique index or primary key violation:"));
		}
	}

	@Test
	public void testInactiveRegion() {
		Region region = Region.create(defaultProvider, "region-1", "region 1", true);

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
		Region.create(defaultProvider, "region-1", "region 1", true);
		Region.create(defaultProvider, "region-2", "region 2", true);

		Provider provider2 = Provider.create("Google");
		Region.create(provider2, "region-3", "region 3", true);

		Set<Region> regions = Region.find.where().eq("provider_uuid", defaultProvider.uuid).findSet();
		assertEquals(regions.size(), 2);
		for (Region region:regions) {
			assertThat(region.code, containsString("region-"));
		}
	}
}
