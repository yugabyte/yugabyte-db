// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.api.controllers;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.controllers.TablesController;

import org.junit.*;
import org.yb.client.ListTablesResponse;
import org.yb.client.YBClient;
import play.libs.Json;
import play.mvc.*;

import static org.junit.Assert.*;
import static play.mvc.Http.Status.OK;

import java.lang.String;
import java.util.Arrays;
import java.util.List;
import static org.mockito.Mockito.*;
import static play.test.Helpers.contentAsString;

public class TablesControllerTest {
  private YBClientService mockService;
  private TablesController tablesController;
  private YBClient mockClient;
  private ListTablesResponse mockResponse;


  @Before
  public void setUp() throws Exception {
    mockClient = mock(YBClient.class);
    mockService = mock(YBClientService.class);
    mockResponse = mock(ListTablesResponse.class);
    when(mockClient.getTablesList()).thenReturn(mockResponse);
    when(mockService.getClient(any(String.class))).thenReturn(mockClient);
    tablesController = new TablesController(mockService);
  }

  @Test
  public void testListTablesSuccess() throws Exception {
    List<String> mockTableNames = Arrays.asList("Table1", "Table2");
    when(mockResponse.getTablesList()).thenReturn(mockTableNames);
    Result r = tablesController.list();
    JsonNode json = Json.parse(contentAsString(r));
    assertEquals(OK, r.status());
    assertTrue(json.get("table_names").isArray());
 }

  @Test
  public void testListTablesFailure() throws Exception {
    when(mockResponse.getTablesList()).thenThrow(new RuntimeException("Unknown Error"));
    Result r = tablesController.list();
    assertEquals(500, r.status());
    assertEquals("Error: Unknown Error", contentAsString(r));
  }
}
