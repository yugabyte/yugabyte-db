// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.models;

import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertSame;
import static org.junit.Assert.assertTrue;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import java.util.ArrayList;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import javax.persistence.PersistenceException;
import org.apache.commons.lang3.RandomStringUtils;
import org.junit.Test;
import play.libs.Json;

public class CustomerTest extends FakeDBApplication {

  @Test
  public void testCreate() {
    for (long i = 0; i < 2; i++) {
      Customer customer = Customer.create("tc", "Test Customer");
      customer.save();
      assertSame(i + 1, customer.getId());
      assertNotNull(customer.getUuid());
      assertEquals("Test Customer", customer.getName());
      assertNotNull(customer.getCreationDate());
    }
  }

  @Test
  public void testCreateWithLargerCustomerCode() {
    String largeCustomerCode = RandomStringUtils.randomAlphabetic(16);
    try {
      Customer customer = Customer.create(largeCustomerCode, "Test Customer");
      customer.save();
    } catch (PersistenceException pe) {
      assertTrue(pe.getMessage().contains("Value too long for column"));
    }
  }

  @Test
  public void testCreateWithCustomerCode() {
    String customerCode = RandomStringUtils.randomAlphabetic(15);
    Customer customer = Customer.create(customerCode, "Test Customer");
    customer.save();
    assertEquals(customerCode, customer.getCode());
  }

  @Test
  public void testCreateValidateUniqueIDs() {
    Customer c1 = Customer.create("C1", "Customer 1");
    c1.save();
    Customer c2 = Customer.create("C2", "Customer 2");
    c2.save();
    assertNotEquals(c1.getId(), c2.getId());
    assertTrue(c2.getId() > c1.getId());
    assertNotEquals(c1.getUuid(), c2.getUuid());
  }

  @Test
  public void findAll() {
    Customer c1 = Customer.create("C1", "Customer 1");
    c1.save();
    Customer c2 = Customer.create("C2", "Customer 2");
    c2.save();

    List<Customer> customerList = Customer.find.all();

    assertEquals(2, customerList.size());
  }

  @Test(expected = javax.persistence.PersistenceException.class)
  public void testInvalidCreate() {
    Customer c = Customer.create(null, null);
    c.save();
  }

  @Test
  public void testUpsertFeatures() {
    Customer c = Customer.create("C1", "Customer 1");
    c.save();

    assertNotNull(c.getUuid());

    JsonNode features =
        Json.parse("{\"TLS\": true, \"universe\": {\"foo\": \"bar\", \"backups\": false}}");
    c.upsertFeatures(features);

    assertEquals(features, c.getFeatures());

    JsonNode newFeatures = Json.parse("{\"universe\": {\"foo\": \"foo\"}}");
    c.upsertFeatures(newFeatures);

    JsonNode expectedFeatures =
        Json.parse("{\"TLS\": true, \"universe\": {\"foo\": \"foo\", \"backups\": false}}");
    assertEquals(expectedFeatures, c.getFeatures());
  }

  @Test
  public void testGetUniversesForProvider() {
    Customer c = ModelFactory.testCustomer();
    Provider p = ModelFactory.awsProvider(c);
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone.createOrThrow(r, "az-2", "PlacementAZ 2", "subnet-2");
    Universe universe = createUniverse(c.getId());
    UniverseDefinitionTaskParams.UserIntent userIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    UUID randProviderUUID = UUID.randomUUID();
    userIntent.provider = randProviderUUID.toString();
    userIntent.regionList = new ArrayList<UUID>();
    userIntent.regionList.add(r.getUuid());
    universe =
        Universe.saveDetails(universe.getUniverseUUID(), ApiUtils.mockUniverseUpdater(userIntent));
    Set<Universe> universes = c.getUniversesForProvider(randProviderUUID);
    assertEquals(1, universes.size());
  }
}
