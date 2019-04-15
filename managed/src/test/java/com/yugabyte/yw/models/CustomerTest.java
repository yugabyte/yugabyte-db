// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.models;

import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import org.apache.commons.lang3.RandomStringUtils;
import org.h2.jdbc.JdbcSQLException;
import org.junit.Test;
import org.mindrot.jbcrypt.BCrypt;

import com.yugabyte.yw.common.FakeDBApplication;

import javax.persistence.PersistenceException;

import java.util.ArrayList;
import java.util.List;
import java.util.Set;
import java.util.UUID;

import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static org.junit.Assert.*;

public class CustomerTest extends FakeDBApplication {

  @Test
  public void testCreate() {
    Customer customer = Customer.create("tc","Test Customer", "foo@bar.com", "password");
    customer.save();
    assertNotNull(customer.uuid);
    assertEquals("foo@bar.com", customer.getEmail());
    assertEquals("Test Customer", customer.name);
    assertNotNull(customer.creationDate);
    assertTrue(BCrypt.checkpw("password", customer.passwordHash));
  }

  @Test
  public void testCreateWithLargerCustomerCode() {
    String largeCustomerCode = RandomStringUtils.randomAlphabetic(16);
    try {
      Customer customer = Customer.create(largeCustomerCode,"Test Customer", "foo@bar.com", "password");
      customer.save();
    } catch (PersistenceException pe) {
      assertTrue(pe.getMessage().contains("Value too long for column"));
    }
  }

  @Test
  public void testCreateWithCustomerCode() {
    String customerCode = RandomStringUtils.randomAlphabetic(15);
    Customer customer = Customer.create(customerCode,"Test Customer", "foo@bar.com", "password");
    customer.save();
    assertEquals(customerCode, customer.code);
  }

  @Test(expected = PersistenceException.class)
  public void testCreateWithDuplicateEmail() {
    Customer c1 = Customer.create("C1", "Customer 1","foo@foo.com", "password");
    c1.save();
    Customer c2 = Customer.create("C2", "Customer 2","foo@foo.com", "password");
    c2.save();
  }

  @Test
  public void testCreateValidateUniqueIDs() {
    Customer c1 = Customer.create("C1",  "Customer 1","foo1@foo.com", "password");
    c1.save();
    Customer c2 = Customer.create("C2", "Customer 2","foo2@foo.com", "password");
    c2.save();
    assertNotEquals(c1.getCustomerId(), c2.getCustomerId());
    assertTrue(c2.getCustomerId() > c1.getCustomerId());
    assertNotEquals(c1.uuid, c2.uuid);
  }

  @Test
  public void findAll() {
    Customer c1 = Customer.create("C1", "Customer 1", "foo@foo.com", "password");
    c1.save();
    Customer c2 = Customer.create("C2", "Customer 2","bar@foo.com", "password");
    c2.save();

    List<Customer> customerList = Customer.find.all();

    assertEquals(2, customerList.size());
  }

  @Test
  public void authenticateWithEmailAndValidPassword() {
    Customer c = Customer.create("C1", "Customer 1","foo@foo.com", "password");
    c.save();
    Customer authCust = Customer.authWithPassword("foo@foo.com", "password");
    assertEquals(authCust.uuid, c.uuid);
  }

  @Test
  public void authenticateWithEmailAndInvalidPassword() {
    Customer c = Customer.create("C1", "Customer 2","foo@foo.com", "password");
    c.save();
    Customer authCust = Customer.authWithPassword("foo@foo.com", "password1");
    assertNull(authCust);
  }

  @Test
  public void testCreateAuthToken() {
    Customer c = Customer.create("C1", "Customer 1","foo@foo.com", "password");
    c.save();

    assertNotNull(c.uuid);

    String authToken = c.createAuthToken();
    assertNotNull(authToken);
    assertNotNull(c.getAuthTokenIssueDate());

    Customer authCust = Customer.authWithToken(authToken);
    assertEquals(authCust.uuid, c.uuid);
  }
  @Test
  public void testAuthTokenExpiry() {
    Customer c1 = Customer.create("C1", "Customer 1","foo@foo.com", "password");
    c1.save();
    assertNotNull(c1.uuid);
    String authTokenOld = c1.createAuthToken();
    assertNotNull(authTokenOld);
    assertNotNull(c1.getAuthTokenIssueDate());
    Customer c2 = Customer.get(c1.uuid);
    String authTokenNew = c2.createAuthToken();
    assertEquals(authTokenNew, authTokenOld);
  }

  @Test
  public void testDeleteAuthToken() {
    Customer c = Customer.create("C1", "Customer 1", "foo@foo.com", "password");
    c.save();

    assertNotNull(c.uuid);

    String authToken = c.createAuthToken();
    assertNotNull(authToken);
    assertNotNull(c.getAuthTokenIssueDate());

    Customer fetchCust = Customer.find.where().eq("uuid", c.uuid).findUnique();
    fetchCust.deleteAuthToken();

    fetchCust = Customer.find.where().eq("uuid", c.uuid).findUnique();
    assertNull(fetchCust.getAuthTokenIssueDate());

    Customer authCust = Customer.authWithToken(authToken);
    assertNull(authCust);
  }

  @Test(expected=javax.persistence.PersistenceException.class)
  public void testInvalidCreate() {
    Customer c = Customer.create(null, null,"foo@bar.com", "password");
    c.save();
  }

  @Test
  public void testUpsertApiToken() {
    Customer c = Customer.create("C1", "Customer 1","foo@foo.com", "password");
    c.save();

    assertNotNull(c.uuid);

    String apiToken = c.upsertApiToken();
    assertNotNull(apiToken);

    Customer apiCust = Customer.authWithApiToken(apiToken);
    assertEquals(apiCust.uuid, c.uuid);
  }

  @Test
  public void testGetUniversesForProvider() {
    Customer c = ModelFactory.testCustomer();
    Provider p = ModelFactory.awsProvider(c);
    Region r = Region.create(p, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone.create(r, "az-1", "PlacementAZ 1", "subnet-1");
    AvailabilityZone.create(r, "az-2", "PlacementAZ 2", "subnet-2");
    Universe universe = createUniverse(c.getCustomerId());
    UniverseDefinitionTaskParams.UserIntent userIntent = new UniverseDefinitionTaskParams.UserIntent();
    UUID randProviderUUID = UUID.randomUUID();
    userIntent.provider = randProviderUUID.toString();
    userIntent.regionList = new ArrayList<UUID>();
    userIntent.regionList.add(r.uuid);
    universe = Universe.saveDetails(universe.universeUUID, ApiUtils.mockUniverseUpdater(userIntent));
    c.addUniverseUUID(universe.universeUUID);
    c.save();
    Set<Universe> universes = c.getUniversesForProvider(randProviderUUID);
    assertEquals(1, universes.size());
  }
}
