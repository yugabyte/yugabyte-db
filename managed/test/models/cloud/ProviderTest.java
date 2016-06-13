// Copyright (c) Yugabyte, Inc.

package models.cloud;

import models.FakeDBApplication;
import org.junit.Test;
import static org.hamcrest.CoreMatchers.*;
import static org.junit.Assert.*;

public class ProviderTest extends FakeDBApplication {
	@Test
	public void testCreate() {
		Provider provider = Provider.create(Provider.Type.AmazonWebService);

		assertNotNull(provider.uuid);
		assertEquals(provider.type, Provider.Type.AmazonWebService);
		assertTrue(provider.isActive());
	}

	@Test
	public void testCreateDuplicateProvider() {
		Provider.create(Provider.Type.AmazonWebService);
		try {
			Provider.create(Provider.Type.AmazonWebService);
		} catch (Exception e) {
			assertThat(e.getMessage(), containsString("Unique index or primary key violation:"));
		}
	}

	@Test
	public void testInactiveProvider() {
		Provider provider = Provider.create(Provider.Type.AmazonWebService);

		assertNotNull(provider.uuid);
		assertEquals(provider.type, Provider.Type.AmazonWebService);
	  assertTrue(provider.isActive());

		provider.setActiveFlag(false);
		provider.save();

		Provider fetch = Provider.find.byId(provider.uuid);
		assertFalse(fetch.isActive());
	}

	@Test
	public void testFindProvider() {
		Provider provider = Provider.create(Provider.Type.AmazonWebService);

		assertNotNull(provider.uuid);
		Provider fetch = Provider.find.byId(provider.uuid);
		assertNotNull(fetch);
		assertEquals(fetch.uuid, provider.uuid);
		assertEquals(fetch.type, provider.type);
		assertTrue(fetch.isActive());
  }
}
