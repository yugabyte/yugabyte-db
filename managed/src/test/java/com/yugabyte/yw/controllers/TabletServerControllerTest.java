// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.net.HostAndPort;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import org.junit.Before;
import org.junit.Test;
import org.yb.client.ListTabletServersResponse;
import org.yb.client.YBClient;
import org.yb.util.ServerInfo;
import play.libs.Json;
import play.mvc.Result;

import java.util.ArrayList;
import java.util.List;
import java.util.UUID;

import static com.yugabyte.yw.common.AssertHelper.assertAuditEntry;
import static com.yugabyte.yw.common.AssertHelper.assertYWSE;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;
import static org.mockito.Matchers.anyString;
import static org.mockito.Mockito.*;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.contentAsString;

public class TabletServerControllerTest extends FakeDBApplication {
  private TabletServerController tabletController;
  private YBClient mockClient;
  private ListTabletServersResponse mockResponse;
  private HostAndPort testHostAndPort = HostAndPort.fromString("0.0.0.0").withDefaultPort(11);

  @Before
  public void setUp() throws Exception {
    mockClient = mock(YBClient.class);
    mockResponse = mock(ListTabletServersResponse.class);
    when(mockClient.listTabletServers()).thenReturn(mockResponse);
    when(mockClient.getLeaderMasterHostAndPort()).thenReturn(testHostAndPort);
    when(mockService.getClient(any())).thenReturn(mockClient);
    when(mockService.getClient(any(), any())).thenReturn(mockClient);
    tabletController = new TabletServerController(mockService);
    when(mockApiHelper.getRequest(anyString())).thenReturn(Json.newObject());
    tabletController.apiHelper = mockApiHelper;
  }

  @Test
  public void testListTabletServersSuccess() {
    when(mockResponse.getTabletServersCount()).thenReturn(2);
    List<ServerInfo> mockTabletSIs = new ArrayList<ServerInfo>();
    ServerInfo si = new ServerInfo("UUID1", "abc", 1001, false, "ALIVE");
    mockTabletSIs.add(si);
    si = new ServerInfo("UUID2", "abc", 2001, true, "ALIVE");
    mockTabletSIs.add(si);
    when(mockResponse.getTabletServersList()).thenReturn(mockTabletSIs);
    Result r = tabletController.list();
    JsonNode json = Json.parse(contentAsString(r));
    assertEquals(OK, r.status());
    assertTrue(json.get("servers").isArray());
  }

  @Test
  public void testListTabletServersFailure() {
    when(mockResponse.getTabletServersCount()).thenThrow(new RuntimeException("Unknown Error"));
    Result result = assertYWSE(() -> tabletController.list());
    assertEquals(500, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals("Error: Unknown Error", json.get("error").asText());
  }

  @Test
  public void testListTabletServersWrapperSuccess() {
    Customer customer = ModelFactory.testCustomer();
    Universe u1 = createUniverse(customer.getCustomerId());
    u1 = Universe.saveDetails(u1.universeUUID, ApiUtils.mockUniverseUpdater());
    customer.addUniverseUUID(u1.universeUUID);
    customer.save();
    Result r = tabletController.listTabletServers(customer.uuid, u1.universeUUID);
    assertEquals(200, r.status());
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testListTabletServersWrapperFailure() {
    when(mockApiHelper.getRequest(anyString())).thenThrow(new RuntimeException("Unknown Error"));
    Customer customer = ModelFactory.testCustomer();
    Universe u1 = createUniverse(customer.getCustomerId());
    u1 = Universe.saveDetails(u1.universeUUID, ApiUtils.mockUniverseUpdater());
    UUID universeUUID = u1.universeUUID;
    customer.addUniverseUUID(u1.universeUUID);
    customer.save();
    Result result =
        assertYWSE(() -> tabletController.listTabletServers(customer.uuid, universeUUID));
    assertEquals(500, result.status());
    assertAuditEntry(0, customer.uuid);
  }
}
