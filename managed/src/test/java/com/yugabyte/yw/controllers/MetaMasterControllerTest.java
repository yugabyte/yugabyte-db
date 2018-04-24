package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.contentAsString;
import static play.test.Helpers.fakeRequest;
import static play.test.Helpers.route;

import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import org.junit.Test;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.FakeDBApplication;

import play.libs.Json;
import play.mvc.Result;

import java.util.HashSet;
import java.util.Set;
import java.util.UUID;

public class MetaMasterControllerTest extends FakeDBApplication {
  public static final Logger LOG = LoggerFactory.getLogger(MetaMasterControllerTest.class);

  @Test
  public void testGetWithInvalidUniverse() {
    String universeUUID = "11111111-2222-3333-4444-555555555555";
    Result result = route(fakeRequest("GET", "/metamaster/universe/" + universeUUID));
    assertRestResult(result, false, BAD_REQUEST);
  }

  @Test
  public void testGetWithValidUniverse() {
    Customer customer = ModelFactory.testCustomer();
    Universe u = createUniverse();
    // Save the updates to the universe.
    Universe.saveDetails(u.universeUUID, ApiUtils.mockUniverseUpdater());
    UserIntent ui = u.getUniverseDetails().getPrimaryCluster().userIntent;
    ui.provider = Provider.get(customer.uuid, Common.CloudType.aws).uuid.toString();
    u.getUniverseDetails().upsertPrimaryCluster(ui, null);

    // Read the value back.
    Result result = route(fakeRequest("GET", "/metamaster/universe/" + u.universeUUID.toString()));
    assertRestResult(result, true, OK);
    // Verify that the correct data is present.
    JsonNode jsonNode = Json.parse(contentAsString(result));
    MetaMasterController.MastersList masterList =
      Json.fromJson(jsonNode, MetaMasterController.MastersList.class);
    Set<String> masterNodeNames = new HashSet<String>();
    masterNodeNames.add("host-n1");
    masterNodeNames.add("host-n2");
    masterNodeNames.add("host-n3");
    for (MetaMasterController.MasterNode node : masterList.masters) {
      assertTrue(masterNodeNames.contains(node.cloudInfo.private_ip));
    }
  }

  @Test
  public void testYqlGetWithInvalidUniverse() {
    testServerGetWithInvalidUniverse(true);
  }

  @Test
  public void testRedisGetWithInvalidUniverse() {
    testServerGetWithInvalidUniverse(false);
  }

  @Test
  public void testYqlGetWithValidUniverse() {
    testServerGetWithValidUniverse(true);
  }

  @Test
  public void testRedisGetWithValidUniverse() {
    testServerGetWithValidUniverse(false);
  }

  private void assertRestResult(Result result, boolean expectSuccess, int expectStatus) {
    assertEquals(expectStatus, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    if (expectSuccess) {
      assertNull(json.get("error"));
    } else {
      assertNotNull(json.get("error"));
      assertFalse(json.get("error").asText().isEmpty());
    }
  }

  private void testServerGetWithInvalidUniverse(boolean isYql) {
    String universeUUID = "11111111-2222-3333-4444-555555555555";
    Customer customer = ModelFactory.testCustomer();
    Result result = route(fakeRequest("GET", "/api/customers/" + customer.uuid + "/universes/" +
                                       universeUUID + (isYql ? "/yqlservers" : "/redisservers")));
    assertRestResult(result, false, BAD_REQUEST);
  }

  private void testServerGetWithValidUniverse(boolean isYql) {
    Customer customer = ModelFactory.testCustomer();
    Universe u1 = createUniverse("Universe-1", customer.getCustomerId());
    u1 = Universe.saveDetails(u1.universeUUID, ApiUtils.mockUniverseUpdater());
    customer.addUniverseUUID(u1.universeUUID);
    customer.save();

    Result r = route(fakeRequest("GET", "/api/customers/" + customer.uuid + "/universes/" +
                                 u1.universeUUID + (isYql ? "/yqlservers" : "/redisservers")));
    assertRestResult(r, true, OK);
  }
}
