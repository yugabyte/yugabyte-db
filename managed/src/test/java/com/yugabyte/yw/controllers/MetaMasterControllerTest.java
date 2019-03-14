package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.commissioner.Common.CloudType.aws;
import static com.yugabyte.yw.commissioner.Common.CloudType.kubernetes;
import static com.yugabyte.yw.common.ApiUtils.getDefaultUserIntent;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.mockito.Matchers.any;
import static org.mockito.Matchers.anyBoolean;
import static org.mockito.Matchers.anyString;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;
import static play.inject.Bindings.bind;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.contentAsString;
import static play.test.Helpers.fakeRequest;
import static play.test.Helpers.route;

import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.KubernetesManager;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.ShellProcessHandler;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import org.junit.Before;
import org.junit.Test;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.FakeDBApplication;

import play.Application;
import play.inject.guice.GuiceApplicationBuilder;
import play.libs.Json;
import play.mvc.Result;
import play.test.Helpers;

import javax.inject.Inject;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.Executors;

public class MetaMasterControllerTest extends FakeDBApplication {

  @Inject
  KubernetesManager mockKubernetesManager;

  Customer defaultCustomer;

  @Override
  protected Application provideApplication() {
    ApiHelper mockApiHelper = mock(ApiHelper.class);
    mockKubernetesManager = mock(KubernetesManager.class);
    Executors mockExecutors = mock(Executors.class);
    return new GuiceApplicationBuilder()
        .configure((Map) Helpers.inMemoryDatabase())
        .overrides(bind(ApiHelper.class).toInstance(mockApiHelper))
        .overrides(bind(KubernetesManager.class).toInstance(mockKubernetesManager))
        .overrides(bind(Executors.class).toInstance(mockExecutors))
        .build();
  }

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
  }

  // TODO: move this to ModelFactory!
  private Universe getKuberentesUniverse() {
    Provider provider = ModelFactory.newProvider(defaultCustomer, kubernetes);
    UserIntent ui = getDefaultUserIntent(provider);
    Universe universe = createUniverse(defaultCustomer.getCustomerId());
    Universe.saveDetails(universe.universeUUID, ApiUtils.mockUniverseUpdater(ui, true));
    defaultCustomer.addUniverseUUID(universe.universeUUID);
    defaultCustomer.save();
    return universe;
  }

  @Test
  public void testGetWithInvalidUniverse() {
    String universeUUID = "11111111-2222-3333-4444-555555555555";
    Result result = route(fakeRequest("GET", "/metamaster/universe/" + universeUUID));
    assertRestResult(result, false, BAD_REQUEST);
  }

  @Test
  public void testGetWithValidUniverse() {
    Universe u = createUniverse("demo-universe", defaultCustomer.getCustomerId());
    // Save the updates to the universe.
    Universe.saveDetails(u.universeUUID, ApiUtils.mockUniverseUpdater("host", aws));
    UserIntent ui = u.getUniverseDetails().getPrimaryCluster().userIntent;
    ui.provider = Provider.get(defaultCustomer.uuid, Common.CloudType.aws).uuid.toString();
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

  Map<String, Integer> endpointPort = ImmutableMap.of(
      "/masters", 7100,
      "/yqlservers", 9042,
      "/redisservers", 6379
  );

  @Test
  public void testServerAddressForKuberenetesServiceFailure() {
    Universe universe = getKuberentesUniverse();
    ShellProcessHandler.ShellResponse re = new ShellProcessHandler.ShellResponse();
    re.code = -1;
    re.message = "Unknown Error!";
    when(mockKubernetesManager.getServiceIPs(any(), anyString(), anyBoolean())).thenReturn(re);

    endpointPort.entrySet().forEach((endpoint) -> {
      String expectedHostString = String.join(",",
          ImmutableList.of("host-n1:" + endpoint.getValue(),
              "host-n2:" + endpoint.getValue(),
              "host-n3:" + endpoint.getValue())
      );

      Result r = route(fakeRequest("GET", "/api/customers/" + defaultCustomer.uuid + "/universes/" +
          universe.universeUUID + endpoint.getKey()));
      JsonNode json = Json.parse(contentAsString(r));
      assertEquals(expectedHostString, json.asText());
    });
  }

  @Test
  public void testServerAddressForKuberenetesServiceWithPodIP() {
    Universe universe = getKuberentesUniverse();
    ShellProcessHandler.ShellResponse re = new ShellProcessHandler.ShellResponse();
    re.code = 0;
    re.message = "12.13.14.15||";
    when(mockKubernetesManager.getServiceIPs(any(), anyString(), anyBoolean())).thenReturn(re);

    endpointPort.entrySet().forEach((endpoint) -> {
      String expectedHostString = "12.13.14.15:" + endpoint.getValue();
      Result r = route(fakeRequest("GET", "/api/customers/" + defaultCustomer.uuid + "/universes/" +
          universe.universeUUID + endpoint.getKey()));
      JsonNode json = Json.parse(contentAsString(r));
      assertEquals(expectedHostString, json.asText());
    });
  }

  @Test
  public void testServerAddressForKuberenetesServiceWithPodAndLoadBalancerIP() {
    Universe universe = getKuberentesUniverse();
    ShellProcessHandler.ShellResponse re = new ShellProcessHandler.ShellResponse();
    re.code = 0;
    re.message = "12.13.14.15|56.78.90.1|";
    when(mockKubernetesManager.getServiceIPs(any(), anyString(), anyBoolean())).thenReturn(re);

    endpointPort.entrySet().forEach((endpoint) -> {
      String expectedHostString = "56.78.90.1:" + endpoint.getValue();
      Result r = route(fakeRequest("GET", "/api/customers/" + defaultCustomer.uuid + "/universes/" +
          universe.universeUUID + endpoint.getKey()));
      JsonNode json = Json.parse(contentAsString(r));
      assertEquals(expectedHostString, json.asText());
    });
  }

  @Test
  public void testServerAddressForKuberenetesServiceWithPodAndLoadBalancerHostname() {
    Universe universe = getKuberentesUniverse();
    ShellProcessHandler.ShellResponse re = new ShellProcessHandler.ShellResponse();
    re.code = 0;
    re.message = "12.13.14.15||loadbalancer.hostname";
    when(mockKubernetesManager.getServiceIPs(any(), anyString(), anyBoolean())).thenReturn(re);

    endpointPort.entrySet().forEach((endpoint) -> {
      String expectedHostString = "loadbalancer.hostname:" + endpoint.getValue();
      Result r = route(fakeRequest("GET", "/api/customers/" + defaultCustomer.uuid + "/universes/" +
          universe.universeUUID + endpoint.getKey()));
      JsonNode json = Json.parse(contentAsString(r));
      assertEquals(expectedHostString, json.asText());
    });
  }

  @Test
  public void testServerAddressForKuberenetesServiceWithPodAndLoadBalancerIpAndHostname() {
    Universe universe = getKuberentesUniverse();
    ShellProcessHandler.ShellResponse re = new ShellProcessHandler.ShellResponse();
    re.code = 0;
    re.message = "12.13.14.15|56.78.90.1|loadbalancer.hostname";
    when(mockKubernetesManager.getServiceIPs(any(), anyString(), anyBoolean())).thenReturn(re);

    endpointPort.entrySet().forEach((endpoint) -> {
      String expectedHostString = "loadbalancer.hostname:" + endpoint.getValue();
      Result r = route(fakeRequest("GET", "/api/customers/" + defaultCustomer.uuid + "/universes/" +
          universe.universeUUID + endpoint.getKey()));
      JsonNode json = Json.parse(contentAsString(r));
      assertEquals(expectedHostString, json.asText());
    });
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
    Result result = route(fakeRequest("GET", "/api/customers/" + defaultCustomer.uuid + "/universes/" +
                                       universeUUID + (isYql ? "/yqlservers" : "/redisservers")));
    assertRestResult(result, false, BAD_REQUEST);
  }

  private void testServerGetWithValidUniverse(boolean isYql) {
    Universe u1 = createUniverse("Universe-1", defaultCustomer.getCustomerId());
    u1 = Universe.saveDetails(u1.universeUUID, ApiUtils.mockUniverseUpdater("host", aws));
    defaultCustomer.addUniverseUUID(u1.universeUUID);
    defaultCustomer.save();

    Result r = route(fakeRequest("GET", "/api/customers/" + defaultCustomer.uuid + "/universes/" +
                                 u1.universeUUID + (isYql ? "/yqlservers" : "/redisservers")));
    assertRestResult(r, true, OK);
  }

  private void testNoYSQLServers() {
    Universe u1 = createUniverse("Universe-1", defaultCustomer.getCustomerId());
    u1 = Universe.saveDetails(u1.universeUUID, ApiUtils.mockUniverseUpdaterWithYSQLNodes(false));
    defaultCustomer.addUniverseUUID(u1.universeUUID);
    defaultCustomer.save();

    Result r = route(fakeRequest("GET", "/api/customers/" + defaultCustomer.uuid + "/universes/" +
      u1.universeUUID + "/ysqlservers"));
    assertRestResult(r, true, OK);
    assertEquals("", Json.parse(contentAsString(r)).asText());
  }

  private void testYSQLServers() {
    Universe u1 = createUniverse("Universe-1", defaultCustomer.getCustomerId());
    u1 = Universe.saveDetails(u1.universeUUID, ApiUtils.mockUniverseUpdaterWithYSQLNodes(true));
    defaultCustomer.addUniverseUUID(u1.universeUUID);
    defaultCustomer.save();

    Result r = route(fakeRequest("GET", "/api/customers/" + defaultCustomer.uuid + "/universes/" +
      u1.universeUUID + "/ysqlservers"));
    assertRestResult(r, true, OK);
    assertEquals("host-n1:5433,host-n2:5433,host-n3:5433", Json.parse(contentAsString(r)).asText());
  }
}
