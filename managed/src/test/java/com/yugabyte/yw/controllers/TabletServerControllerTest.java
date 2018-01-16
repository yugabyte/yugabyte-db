// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import static org.junit.Assert.*;
import static play.mvc.Http.Status.OK;
import static org.mockito.Mockito.*;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.services.YBClientService;

import java.lang.String;
import java.util.ArrayList;
import java.util.List;
import org.junit.*;
import play.libs.Json;
import play.mvc.*;

import org.yb.client.ListTabletServersResponse;
import org.yb.client.YBClient;
import org.yb.util.ServerInfo;

public class TabletServerControllerTest {
  private YBClientService mockService;
  private TabletServerController tabletController;
  private YBClient mockClient;
  private ListTabletServersResponse mockResponse;


  @Before
  public void setUp() throws Exception {
    mockClient = mock(YBClient.class);
    mockService = mock(YBClientService.class);
    mockResponse = mock(ListTabletServersResponse.class);
    when(mockClient.listTabletServers()).thenReturn(mockResponse);
    when(mockService.getClient(any(String.class))).thenReturn(mockClient);
    tabletController = new TabletServerController(mockService);
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
}
