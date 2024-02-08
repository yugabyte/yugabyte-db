package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.commissioner.Common.CloudType.aws;
import static com.yugabyte.yw.commissioner.Common.CloudType.kubernetes;
import static com.yugabyte.yw.common.ApiUtils.getDefaultUserIntent;
import static com.yugabyte.yw.common.ApiUtils.getDefaultUserIntentSingleAZ;
import static com.yugabyte.yw.common.AssertHelper.assertAuditEntry;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.any;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.contentAsString;
import static play.test.Helpers.fakeRequest;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import java.util.Collections;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.TimeoutException;
import org.junit.Before;
import org.junit.Test;
import org.yb.client.ListMastersResponse;
import org.yb.client.YBClient;
import play.libs.Json;
import play.mvc.Result;

public class MetaMasterControllerTest extends FakeDBApplication {

  private YBClient mockClient;
  Customer defaultCustomer;
  Users defaultUser;

  @Before
  public void setUp() {
    try {
      defaultCustomer = ModelFactory.testCustomer();
      defaultUser = ModelFactory.testUser(defaultCustomer);
      mockClient = mock(YBClient.class);
      when(mockService.getClient(any())).thenReturn(mockClient);
      when(mockService.getClient(any(), any())).thenReturn(mockClient);
      ListMastersResponse listMastersResponse = mock(ListMastersResponse.class);
      when(listMastersResponse.getMasters()).thenReturn(Collections.emptyList());
      when(mockClient.listMasters()).thenReturn(listMastersResponse);
    } catch (Exception e) {
      fail();
    }
  }

  // TODO: move this to ModelFactory!
  private Universe getKubernetesUniverse(boolean isMultiAz) {
    Provider provider = ModelFactory.newProvider(defaultCustomer, kubernetes);
    provider.setConfigMap(ImmutableMap.of("KUBECONFIG", "test"));
    provider.save();
    UserIntent ui =
        isMultiAz ? getDefaultUserIntent(provider) : getDefaultUserIntentSingleAZ(provider);
    Universe universe = createUniverse(defaultCustomer.getId());
    Universe.saveDetails(universe.getUniverseUUID(), ApiUtils.mockUniverseUpdater(ui, true));
    return universe;
  }

  @Test
  public void testGetWithInvalidUniverse()
      throws InterruptedException, ExecutionException, TimeoutException {
    String universeUUID = "11111111-2222-3333-4444-555555555555";
    Result result =
        routeWithYWErrHandler(fakeRequest("GET", "/metamaster/universe/" + universeUUID));
    assertRestResult(result, false, BAD_REQUEST);
    assertAuditEntry(0, defaultCustomer.getUuid());
  }

  @Test
  public void testGetWithValidUniverse() {
    Universe u = createUniverse("demo-universe", defaultCustomer.getId());
    // Save the updates to the universe.
    Universe.saveDetails(u.getUniverseUUID(), ApiUtils.mockUniverseUpdater("host", aws));
    UserIntent ui = u.getUniverseDetails().getPrimaryCluster().userIntent;
    ui.provider =
        Provider.get(defaultCustomer.getUuid(), Common.CloudType.aws).get(0).getUuid().toString();
    u.getUniverseDetails().upsertPrimaryCluster(ui, null);

    // Read the value back.
    Result result =
        route(fakeRequest("GET", "/metamaster/universe/" + u.getUniverseUUID().toString()));
    assertRestResult(result, true, OK);
    // Verify that the correct data is present.
    JsonNode jsonNode = Json.parse(contentAsString(result));
    MetaMasterController.MastersList masterList =
        Json.fromJson(jsonNode, MetaMasterController.MastersList.class);
    Set<String> masterNodeNames = new HashSet<>();
    masterNodeNames.add("10.0.0.1");
    masterNodeNames.add("10.0.0.2");
    masterNodeNames.add("10.0.0.3");
    for (MetaMasterController.MasterNode node : masterList.masters) {
      assertTrue(masterNodeNames.contains(node.cloudInfo.private_ip));
    }
    assertAuditEntry(0, defaultCustomer.getUuid());
  }

  @Test
  public void testYqlGetWithInvalidUniverse() {
    testServerGetWithInvalidUniverse(true);
  }

  @Test
  public void testGetYsqlServers() {
    testYSQLServers();
  }

  @Test
  public void testGetNoYsqlServers() {
    testNoYSQLServers();
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

  Map<String, Integer> endpointPort =
      ImmutableMap.of(
          "/masters", 7100,
          "/yqlservers", 9042,
          "/redisservers", 6379);

  Map<String, Integer> endpointPortYSQL =
      ImmutableMap.of(
          "/masters", 7100,
          "/yqlservers", 9042,
          "/redisservers", 6379,
          "/ysqlservers", 5433);

  @Test
  public void testServerAddressForKuberenetesServiceFailure() {
    Universe universe = getKubernetesUniverse(false);
    when(mockKubernetesManager.getPreferredServiceIP(
            any(), anyString(), anyString(), anyBoolean(), anyBoolean()))
        .thenReturn(null);

    endpointPort.forEach(
        (key, value) -> {
          String expectedHostString =
              String.join(
                  ",",
                  ImmutableList.of("10.0.0.1:" + value, "10.0.0.2:" + value, "10.0.0.3:" + value));

          Result r =
              route(
                  fakeRequest(
                      "GET",
                      "/api/customers/"
                          + defaultCustomer.getUuid()
                          + "/universes/"
                          + universe.getUniverseUUID()
                          + key));
          JsonNode json = Json.parse(contentAsString(r));
          if (key == "/masters/info") {
            assertEquals("", json.asText());
          } else {
            assertEquals(expectedHostString, json.asText());
          }
        });
    assertAuditEntry(0, defaultCustomer.getUuid());
  }

  @Test
  public void testServerAddressForKuberenetesServiceWithPodIP() {
    Universe universe = getKubernetesUniverse(false);
    when(mockKubernetesManager.getPreferredServiceIP(
            any(), anyString(), anyString(), anyBoolean(), anyBoolean()))
        .thenReturn("12.13.14.15");

    endpointPortYSQL.forEach(
        (key, value) -> {
          String expectedHostString = "12.13.14.15:" + value;
          Result r =
              route(
                  fakeRequest(
                      "GET",
                      "/api/customers/"
                          + defaultCustomer.getUuid()
                          + "/universes/"
                          + universe.getUniverseUUID()
                          + key));
          JsonNode json = Json.parse(contentAsString(r));
          if (key == "/masters/info") {
            assertEquals("", json.asText());
          } else {
            assertEquals(expectedHostString, json.asText());
          }
        });
    assertAuditEntry(0, defaultCustomer.getUuid());
  }

  @Test
  public void testServerAddressForKuberenetesServiceWithPodIPMultiCluster() {
    Universe universe = getKubernetesUniverse(true);
    when(mockKubernetesManager.getPreferredServiceIP(
            any(), anyString(), anyString(), anyBoolean(), anyBoolean()))
        .thenReturn("12.13.14.15");

    endpointPort.forEach(
        (key, value) -> {
          String expectedHostString = "12.13.14.15:" + value;
          String completeString = String.format("%s,%s", expectedHostString, expectedHostString);
          Result r =
              route(
                  fakeRequest(
                      "GET",
                      "/api/customers/"
                          + defaultCustomer.getUuid()
                          + "/universes/"
                          + universe.getUniverseUUID()
                          + key));
          JsonNode json = Json.parse(contentAsString(r));
          if (key == "/masters/info") {
            assertEquals("", json.asText());
          } else {
            assertEquals(completeString, json.asText());
          }
        });
    assertAuditEntry(0, defaultCustomer.getUuid());
  }

  @Test
  public void testServerAddressForKuberenetesServiceWithPodAndLoadBalancerIP() {
    Universe universe = getKubernetesUniverse(false);
    when(mockKubernetesManager.getPreferredServiceIP(
            any(), anyString(), anyString(), anyBoolean(), anyBoolean()))
        .thenReturn("56.78.90.1");

    endpointPort.forEach(
        (key, value) -> {
          String expectedHostString = "56.78.90.1:" + value;
          Result r =
              route(
                  fakeRequest(
                      "GET",
                      "/api/customers/"
                          + defaultCustomer.getUuid()
                          + "/universes/"
                          + universe.getUniverseUUID()
                          + key));
          JsonNode json = Json.parse(contentAsString(r));
          if (key == "/masters/info") {
            assertEquals("", json.asText());
          } else {
            assertEquals(expectedHostString, json.asText());
          }
        });
    assertAuditEntry(0, defaultCustomer.getUuid());
  }

  @Test
  public void testServerAddressForKuberenetesServiceWithPodAndLoadBalancerHostname() {
    Universe universe = getKubernetesUniverse(false);
    when(mockKubernetesManager.getPreferredServiceIP(
            any(), anyString(), anyString(), anyBoolean(), anyBoolean()))
        .thenReturn("loadbalancer.hostname");

    endpointPort.forEach(
        (key, value) -> {
          String expectedHostString = "loadbalancer.hostname:" + value;
          Result r =
              route(
                  fakeRequest(
                      "GET",
                      "/api/customers/"
                          + defaultCustomer.getUuid()
                          + "/universes/"
                          + universe.getUniverseUUID()
                          + key));
          JsonNode json = Json.parse(contentAsString(r));
          if (key == "/masters/info") {
            assertEquals("", json.asText());
          } else {
            assertEquals(expectedHostString, json.asText());
          }
        });
    assertAuditEntry(0, defaultCustomer.getUuid());
  }

  @Test
  public void testServerAddressForKuberenetesServiceWithPodAndLoadBalancerIpAndHostname() {
    Universe universe = getKubernetesUniverse(false);
    when(mockKubernetesManager.getPreferredServiceIP(
            any(), anyString(), anyString(), anyBoolean(), anyBoolean()))
        .thenReturn("loadbalancer.hostname");

    endpointPort.forEach(
        (key, value) -> {
          String expectedHostString = "loadbalancer.hostname:" + value;
          Result r =
              route(
                  fakeRequest(
                      "GET",
                      "/api/customers/"
                          + defaultCustomer.getUuid()
                          + "/universes/"
                          + universe.getUniverseUUID()
                          + key));
          JsonNode json = Json.parse(contentAsString(r));
          if (key == "/masters/info") {
            assertEquals("", json.asText());
          } else {
            assertEquals(expectedHostString, json.asText());
          }
        });
    assertAuditEntry(0, defaultCustomer.getUuid());
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
    assertAuditEntry(0, defaultCustomer.getUuid());
  }

  private void testServerGetWithInvalidUniverse(boolean isYql) {
    String universeUUID = "11111111-2222-3333-4444-555555555555";
    Result result =
        assertPlatformException(
            () ->
                route(
                    fakeRequest(
                        "GET",
                        "/api/customers/"
                            + defaultCustomer.getUuid()
                            + "/universes/"
                            + universeUUID
                            + (isYql ? "/yqlservers" : "/redisservers"))));
    assertRestResult(result, false, BAD_REQUEST);
    assertAuditEntry(0, defaultCustomer.getUuid());
  }

  private void testServerGetWithValidUniverse(boolean isYql) {
    Universe u1 = createUniverse("Universe-1", defaultCustomer.getId());
    u1 = Universe.saveDetails(u1.getUniverseUUID(), ApiUtils.mockUniverseUpdater("host", aws));

    Result r =
        route(
            fakeRequest(
                "GET",
                "/api/customers/"
                    + defaultCustomer.getUuid()
                    + "/universes/"
                    + u1.getUniverseUUID()
                    + (isYql ? "/yqlservers" : "/redisservers")));
    assertRestResult(r, true, OK);
    assertAuditEntry(0, defaultCustomer.getUuid());
  }

  private void testNoYSQLServers() {
    Universe u1 = createUniverse("Universe-1", defaultCustomer.getId());
    u1 =
        Universe.saveDetails(
            u1.getUniverseUUID(), ApiUtils.mockUniverseUpdaterWithYSQLNodes(false));

    Result r =
        route(
            fakeRequest(
                "GET",
                "/api/customers/"
                    + defaultCustomer.getUuid()
                    + "/universes/"
                    + u1.getUniverseUUID()
                    + "/ysqlservers"));
    assertRestResult(r, true, OK);
    assertEquals("", Json.parse(contentAsString(r)).asText());
    assertAuditEntry(0, defaultCustomer.getUuid());
  }

  private void testYSQLServers() {
    Universe u1 = createUniverse("Universe-1", defaultCustomer.getId());
    u1 =
        Universe.saveDetails(u1.getUniverseUUID(), ApiUtils.mockUniverseUpdaterWithYSQLNodes(true));

    Result r =
        route(
            fakeRequest(
                "GET",
                "/api/customers/"
                    + defaultCustomer.getUuid()
                    + "/universes/"
                    + u1.getUniverseUUID()
                    + "/ysqlservers"));
    assertRestResult(r, true, OK);
    assertEquals(
        "10.0.0.1:5433,10.0.0.2:5433,10.0.0.3:5433", Json.parse(contentAsString(r)).asText());
    assertAuditEntry(0, defaultCustomer.getUuid());
  }
}
