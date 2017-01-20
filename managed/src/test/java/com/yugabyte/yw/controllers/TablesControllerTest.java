// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;
import static org.mockito.Matchers.any;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.contentAsString;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashSet;
import java.util.Iterator;
import java.util.List;
import java.util.Set;
import java.util.UUID;

import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import org.junit.Before;
import org.junit.Test;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.Common.TableType;
import org.yb.client.ListTablesResponse;
import org.yb.client.YBClient;
import org.yb.client.shaded.com.google.protobuf.ByteString;
import org.yb.master.Master.ListTablesResponsePB.TableInfo;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;

import play.libs.Json;
import play.mvc.Result;

public class TablesControllerTest extends FakeDBApplication {
  public static final Logger LOG = LoggerFactory.getLogger(TablesControllerTest.class);
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
  public void testListTablesFromYbClient() throws Exception {
    List<TableInfo> tableInfoList = new ArrayList<TableInfo>();
    Set<String> tableNames = new HashSet<String>();
    tableNames.add("Table1");
    tableNames.add("Table2");
    TableInfo ti1 = TableInfo.newBuilder()
                             .setName("Table1")
                             .setId(ByteString.copyFromUtf8(UUID.randomUUID().toString()))
                             .setTableType(TableType.REDIS_TABLE_TYPE)
                             .build();
    TableInfo ti2 = TableInfo.newBuilder()
                             .setName("Table2")
                             .setId(ByteString.copyFromUtf8(UUID.randomUUID().toString()))
                             .setTableType(TableType.YSQL_TABLE_TYPE)
                             .build();
    tableInfoList.add(ti1);
    tableInfoList.add(ti2);
    when(mockResponse.getTableInfoList()).thenReturn(tableInfoList);

    Customer customer = Customer.create("Valid Customer", "abd@def.ghi", "password");
    Universe u1 = Universe.create("Universe-1", UUID.randomUUID(), customer.getCustomerId());
    u1 = Universe.saveDetails(u1.universeUUID, ApiUtils.mockUniverseUpdater());
    customer.addUniverseUUID(u1.universeUUID);
    customer.save();

    LOG.info("Created customer " + customer.uuid + " with universe " + u1.universeUUID);
    Result r = tablesController.universeList(customer.uuid, u1.universeUUID);
    JsonNode json = Json.parse(contentAsString(r));
    LOG.info("Fetched table list from universe, response: " + contentAsString(r));
    assertEquals(OK, r.status());
    assertTrue(json.isArray());
    Iterator<JsonNode> it = json.elements();
    int numTables = 0;
    while (it.hasNext()) {
      numTables++;
      JsonNode table = it.next();
      String tableName = table.get("tableName").asText();
      String tableType = table.get("tableType").asText();
      LOG.info("Table name: " + tableName + ", table type: " + tableType);
      assertTrue(tableNames.contains(tableName));
      if (tableName.equals("Table1")) {
        assertEquals(TableType.REDIS_TABLE_TYPE.toString(), tableType);
      } else if (tableName.equals("Table2")) {
        assertEquals(TableType.YSQL_TABLE_TYPE.toString(), tableType);
      }
    }
    LOG.info("Processed " + numTables + " tables");
    assertEquals(numTables, tableNames.size());
 }

  @Test
  public void testListTablesFailure() throws Exception {
    when(mockResponse.getTablesList()).thenThrow(new RuntimeException("Unknown Error"));
    Result r = tablesController.list();
    assertEquals(500, r.status());
    assertEquals("Error: Unknown Error", contentAsString(r));
  }

  @Test
  public void testUniverseListMastersNotQueryable() {

    Customer customer = Customer.create("Valid Customer", "abd@def.ghi", "password");
    Universe u1 = Universe.create("Universe-1", UUID.randomUUID(), customer.getCustomerId());
    customer.addUniverseUUID(u1.universeUUID);
    customer.save();

    Result r = tablesController.universeList(customer.uuid, u1.universeUUID);
    assertEquals(500, r.status());
    assertEquals("{\"error\":\"Masters are not currently queryable.\"}", contentAsString(r));
  }
}
