// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import static org.junit.Assert.*;
import static play.mvc.Http.Status.OK;
import static org.mockito.Mockito.*;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.net.HostAndPort;

import java.lang.String;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;
import org.junit.*;
import play.libs.Json;
import play.mvc.*;
import play.test.WithApplication;

import org.yb.client.ListTabletServersResponse;
import org.yb.client.YBClient;
import org.yb.util.ServerInfo;

import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.ApiResponse;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.FakeApiHelper;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;

import static play.inject.Bindings.bind;
import play.Application;
import play.inject.guice.GuiceApplicationBuilder;

public class TabletServerControllerTest extends WithApplication {
  private YBClientService mockService;
  private TabletServerController tabletController;
  private YBClient mockClient;
  private ListTabletServersResponse mockResponse;
  private ApiHelper mockApiHelper;
  private HostAndPort testHostAndPort = HostAndPort.fromString("0.0.0.0").withDefaultPort(11);

  @Override
  protected Application provideApplication() {
    mockApiHelper = mock(ApiHelper.class);
    mockService = mock(YBClientService.class);
    return new GuiceApplicationBuilder()
            .overrides(bind(ApiHelper.class).toInstance(mockApiHelper))
            .overrides(bind(YBClientService.class).toInstance(mockService))
            .build();
  }

  @Before
  public void setUp() throws Exception {
    mockClient = mock(YBClient.class);
    mockResponse = mock(ListTabletServersResponse.class);
    when(mockClient.listTabletServers()).thenReturn(mockResponse);
    when(mockService.getClient(any(String.class))).thenReturn(mockClient);
    when(mockService.getClient(any(String.class), any(String.class))).thenReturn(mockClient);
    when(mockClient.getLeaderMasterHostAndPort()).thenReturn(testHostAndPort);
    tabletController = new TabletServerController(mockService);
    when(mockApiHelper.getRequest(any(String.class))).thenReturn(Json.newObject());
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
    Result r = tabletController.list();
    assertEquals(500, r.status());
    assertEquals("Error: Unknown Error", contentAsString(r));
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
  }

  @Test
  public void testListTabletServersWrapperFailure() {
    when(mockApiHelper.getRequest(any(String.class)))
            .thenThrow(new RuntimeException("Unknown Error"));
    Customer customer = ModelFactory.testCustomer();
    Universe u1 = createUniverse(customer.getCustomerId());
    u1 = Universe.saveDetails(u1.universeUUID, ApiUtils.mockUniverseUpdater());
    customer.addUniverseUUID(u1.universeUUID);
    customer.save();
    Result r = tabletController.listTabletServers(customer.uuid, u1.universeUUID);
    assertEquals(500, r.status());
  }
}
