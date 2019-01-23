// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.AssertHelper.*;
import static com.yugabyte.yw.common.FakeApiHelper.doRequestWithAuthToken;
import static com.yugabyte.yw.common.FakeApiHelper.doRequestWithAuthTokenAndBody;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThat;
import static org.hamcrest.CoreMatchers.allOf;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.hamcrest.core.StringContains.containsString;
import static org.mockito.Matchers.any;
import static org.mockito.Matchers.anyLong;
import static org.mockito.Mockito.doNothing;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.contentAsString;

import java.util.*;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.CommissionerBaseTest;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.NodeActionType;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.ImportUniverseFormData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ImportedState;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Capability;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;

import org.junit.Before;
import org.junit.Ignore;
import org.junit.Test;
import org.yb.client.YBClient;
import org.yb.client.ListTabletServersResponse;
import org.yb.util.ServerInfo;

import play.Application;
import play.inject.guice.GuiceApplicationBuilder;
import play.libs.Json;
import play.mvc.Result;
import play.test.Helpers;
import play.test.WithApplication;

public class ImportControllerTest extends CommissionerBaseTest {
  private static String MASTER_ADDRS = "127.0.0.1:7100,127.0.0.2:7100,127.0.0.3:7100";
  private Customer customer;
  private String authToken;
  private YBClient mockClient;
  private ListTabletServersResponse mockResponse;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    authToken = customer.createAuthToken();
    mockClient = mock(YBClient.class);
    mockResponse = mock(ListTabletServersResponse.class);
    when(mockYBClient.getClient(any())).thenReturn(mockClient);
    when(mockClient.waitForServer(any(), anyLong())).thenReturn(true);
    when(mockResponse.getTabletServersCount()).thenReturn(3);
    List<ServerInfo> mockTabletSIs = new ArrayList<ServerInfo>();
    ServerInfo si = new ServerInfo("UUID1", "127.0.0.1", 9100, false, "ALIVE");
    mockTabletSIs.add(si);
    si = new ServerInfo("UUID2", "127.0.0.2", 9100, false, "ALIVE");
    mockTabletSIs.add(si);
    si = new ServerInfo("UUID3", "127.0.0.3", 9100, false, "ALIVE");
    mockTabletSIs.add(si);
    when(mockResponse.getTabletServersList()).thenReturn(mockTabletSIs);
    try {
      when(mockClient.listTabletServers()).thenReturn(mockResponse);
      doNothing().when(mockClient).waitForMasterLeader(anyLong());
    } catch (Exception e) {
      e.printStackTrace();
    }
  }

  @Test
  public void testImportUniverse() {
    String url = "/api/customers/" + customer.uuid + "/universes/import";
    ObjectNode bodyJson = Json.newObject()
                              .put("universeName", "importUniv")
                              .put("masterAddresses", MASTER_ADDRS);
    // Master phase
    Result result = doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson);
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    String univUUID = json.get("universeUUID").asText();
    assertNotNull(univUUID);
    assertNotNull(json.get("checks").get("create_db_entry"));
    assertEquals(json.get("checks").get("create_db_entry").asText(), "OK");
    assertNotNull(json.get("checks").get("check_masters_are_running"));
    assertEquals(json.get("checks").get("check_masters_are_running").asText(), "OK");
    assertNotNull(json.get("checks").get("check_master_leader_election"));
    assertEquals(json.get("checks").get("check_master_leader_election").asText(), "OK");
    assertNotNull(json.get("state").asText());
    assertEquals(json.get("state").asText(), "IMPORTED_MASTERS");
    Universe universe = Universe.get(UUID.fromString(univUUID));
    assertEquals(universe.getUniverseDetails().importedState, ImportedState.MASTERS_ADDED);
    assertEquals(universe.getUniverseDetails().capability, Capability.READ_ONLY);

    String tUnivUUID = json.get("universeUUID").asText();
    assertEquals(univUUID, tUnivUUID);
    // Tserver phase
    bodyJson.put("currentState", json.get("state").asText())
            .put("universeUUID", univUUID);
    result = doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson);
    assertOk(result);
    json = Json.parse(contentAsString(result));
    assertEquals(json.get("state").asText(), "IMPORTED_TSERVERS");
    assertNotNull(json.get("universeUUID"));
    assertNotNull(json.get("checks").get("find_tservers_list"));
    assertEquals(json.get("checks").get("find_tservers_list").asText(), "OK");
    assertNotNull(json.get("checks").get("check_tservers_are_running"));
    assertEquals(json.get("checks").get("check_tservers_are_running").asText(), "OK");
    assertNotNull(json.get("checks").get("check_tserver_heartbeats"));
    assertEquals(json.get("checks").get("check_tserver_heartbeats").asText(), "OK");
    assertNotNull(json.get("tservers_list").asText());
    assertValue(json, "tservers_count", "3");
    universe = Universe.get(UUID.fromString(univUUID));
    assertEquals(universe.getUniverseDetails().importedState, ImportedState.TSERVERS_ADDED);
    assertEquals(universe.getUniverseDetails().capability, Capability.READ_ONLY);

    // Finish
    bodyJson.put("currentState", json.get("state").asText());
    result = doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson);
    assertOk(result);
    json = Json.parse(contentAsString(result));
    assertValue(json, "state", "FINISHED");
    assertNotNull(json.get("checks").get("create_prometheus_config"));
    assertEquals(json.get("checks").get("create_prometheus_config").asText(), "OK");
    assertThat(json.get("checks").get("node_exporter").asText(),
        allOf(notNullValue(), containsString("OK")));
    assertThat(json.get("checks").get("node_exporter_ip_error_map").asText(),
            allOf(notNullValue(), containsString("127.0.0")));
    assertEquals(json.get("universeUUID").asText(), univUUID);
    assertEquals(universe.getUniverseDetails().capability, Capability.READ_ONLY);

    // Confirm customer knows about this universe and has correct node names/ips.
    url = "/api/customers/" + customer.uuid + "/universes/" + univUUID;
    result = doRequestWithAuthToken("GET", url, authToken);
    assertOk(result);
    json = Json.parse(contentAsString(result));
    JsonNode universeDetails = json.get("universeDetails");
    assertNotNull(universeDetails);
    JsonNode nodeDetailsMap = universeDetails.get("nodeDetailsSet");
    assertNotNull(nodeDetailsMap);
    int numNodes = 0;
    for (Iterator<JsonNode> it = nodeDetailsMap.elements(); it.hasNext();) {
      JsonNode node = it.next();
      int nodeIdx = node.get("nodeIdx").asInt();
      assertValue(node, "nodeName", "yb-importUniv-n" + nodeIdx);
      assertThat(node.get("cloudInfo").get("private_ip").asText(),
                 allOf(notNullValue(), containsString("127.0.0")));
      numNodes++;
    }
    assertEquals(3, numNodes);

    // Provider should have the instance type.
    UUID provUUID = Provider.get(customer.uuid, CloudType.other).uuid;
    url = "/api/customers/" + customer.uuid + "/providers/" + provUUID + "/instance_types/" +
          ImportUniverseFormData.DEFAULT_INSTANCE;
    result = doRequestWithAuthToken("GET", url, authToken);
    assertOk(result);
    json = Json.parse(contentAsString(result));
    assertEquals(json.get("instanceTypeCode").asText(), ImportUniverseFormData.DEFAULT_INSTANCE);
    assertEquals(json.get("providerCode").asText(), CloudType.other.name());

    // Edit should fail.
    bodyJson = Json.newObject();
    ObjectNode userIntentJson = Json.newObject()
      .put("universeName", universe.name)
      .put("numNodes", 5)
      .put("replicationFactor", 3);
    bodyJson.set("clusters", Json.newArray().add(Json.newObject().set("userIntent", userIntentJson)));
    url = "/api/customers/" + customer.uuid + "/universes/" + univUUID;
    result = doRequestWithAuthTokenAndBody("PUT", url, authToken, bodyJson);
    assertBadRequest(result, "cannot be edited");

    // Node ops should fail.
    url = "/api/customers/" + customer.uuid + "/universes/" + univUUID + "/nodes/" +
          nodeDetailsMap.elements().next().get("nodeName").asText();
    bodyJson.put("nodeAction", NodeActionType.REMOVE.name());
    result = doRequestWithAuthTokenAndBody("PUT", url, authToken, bodyJson);
    assertBadRequest(result, "Node actions cannot be performed on universe");

    // Delete should succeed.
    url = "/api/customers/" + customer.uuid + "/universes/" + univUUID;
    result = doRequestWithAuthToken("DELETE", url, authToken);
    assertOk(result);
    json = Json.parse(contentAsString(result));
    UUID taskUUID = UUID.fromString(json.get("taskUUID").asText());
    TaskInfo deleteTaskInfo = null;
    try {
      deleteTaskInfo = waitForTask(taskUUID);
    } catch (InterruptedException e) {
      assertNull(e.getMessage());
    }
    assertNotNull(deleteTaskInfo);
    assertValue(Json.toJson(deleteTaskInfo), "taskState", "Success");

    url = "/api/customers/" + customer.uuid + "/universes/" + univUUID;
    result = doRequestWithAuthToken("GET", url, authToken);
    assertBadRequest(result, "Invalid Universe UUID");

    try {
      universe = Universe.get(UUID.fromString(univUUID));
    } catch (RuntimeException e) {
      assertThat(e.getMessage(),
                 allOf(notNullValue(), containsString("Cannot find universe")));
    }
  }

  @Test
  public void testInvalidAddressImport() {
    String url = "/api/customers/" + customer.uuid + "/universes/import";
    ObjectNode bodyJson = Json.newObject()
                              .put("universeName", "importUniv")
                              .put("masterAddresses", "incorrect_format");
    Result result = doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson);
    assertBadRequest(result, "Could not parse host:port from masterAddresseses: incorrect_format");
  }

  @Test
  public void testInvalidStateImport() {
    String url = "/api/customers/" + customer.uuid + "/universes/import";
    ObjectNode bodyJson = Json.newObject()
                              .put("universeName", "importUniv")
                              .put("masterAddresses", MASTER_ADDRS)
                              .put("currentState",
                                   ImportUniverseFormData.State.IMPORTED_TSERVERS.name());
    Result result = doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson);
    assertBadRequest(result, "Valid universe uuid needs to be set.");
  }

  @Test
  public void testFailedMasterImport() {
    when(mockClient.waitForServer(any(), anyLong())).thenThrow(IllegalStateException.class);
    String url = "/api/customers/" + customer.uuid + "/universes/import";
    ObjectNode bodyJson = Json.newObject()
                              .put("universeName", "importUniv")
                              .put("masterAddresses", MASTER_ADDRS);
    // Master phase
    Result result = doRequestWithAuthTokenAndBody("POST", url, authToken, bodyJson);
    assertInternalServerError(result, "java.lang.RuntimeException: WaitForServer");
    JsonNode resultJson = Json.parse(contentAsString(result));
    String univUUID = resultJson.get("error").get("universeUUID").asText();
    assertNotNull(univUUID);
    assertNotNull(resultJson.get("error").get("checks").get("create_db_entry"));
    assertEquals(resultJson.get("error").get("checks").get("create_db_entry").asText(), "OK");
    assertNotNull(resultJson.get("error").get("checks").get("check_masters_are_running"));
    assertEquals(resultJson.get("error").get("checks").get("check_masters_are_running").asText(),
                 "FAILURE");
    Universe universe = Universe.get(UUID.fromString(univUUID));
    assertEquals(universe.getUniverseDetails().importedState, ImportedState.STARTED);
    assertEquals(universe.getUniverseDetails().capability, Capability.READ_ONLY);
    assertFalse(universe.getUniverseDetails().isUniverseEditable());
  }
}
