// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import static org.hamcrest.CoreMatchers.allOf;
import static org.hamcrest.CoreMatchers.containsString;
import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.is;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertThat;
import static org.junit.Assert.assertTrue;
import static org.mockito.Matchers.any;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;
import static play.inject.Bindings.bind;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.contentAsString;
import static play.test.Helpers.fakeRequest;
import static play.test.Helpers.route;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashSet;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;

import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.FakeApiHelper;
import com.yugabyte.yw.forms.TableDefinitionTaskParams;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.TaskInfo;
import org.junit.Before;
import org.junit.Test;
import org.mockito.Matchers;
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

import play.Application;
import play.inject.guice.GuiceApplicationBuilder;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Result;
import play.test.Helpers;

public class TablesControllerTest extends FakeDBApplication {
  public static final Logger LOG = LoggerFactory.getLogger(TablesControllerTest.class);
  private YBClientService mockService;
  private TablesController tablesController;
  private YBClient mockClient;
  private ListTablesResponse mockResponse;
  private Commissioner mockCommissioner;

  @Override
  protected Application provideApplication() {
    mockCommissioner = mock(Commissioner.class);
    return new GuiceApplicationBuilder()
        .configure((Map) Helpers.inMemoryDatabase())
        .overrides(bind(Commissioner.class).toInstance(mockCommissioner))
        .build();
  }

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
    assertEquals(200, r.status());
    assertEquals("Expected error. Masters are not currently queryable.", contentAsString(r));
  }

  @Test
  public void testCreateCassandraTableWithInvalidUUID() {
    Customer customer = Customer.create("Valid Customer", "abd@def.ghi", "password");
    String authToken = customer.createAuthToken();
    customer.save();

    UUID badUUID = UUID.randomUUID();
    String method = "POST";
    String url = "/api/" + customer.uuid + "/universes/" + badUUID + "/tables";
    ObjectNode emptyJson = Json.newObject();

    Result result = FakeApiHelper.doRequestWithAuthTokenAndBody(method, url, authToken, emptyJson);
    assertEquals(BAD_REQUEST, result.status());
    assertThat(contentAsString(result), is(containsString("Invalid Universe UUID: " + badUUID)));
  }

  @Test
  public void testCreateCassandraTableWithInvalidParams() {
    Customer customer = Customer.create("Valid Customer", "abd@def.ghi", "password");
    String authToken = customer.createAuthToken();
    Universe universe = Universe.create("Universe-1", UUID.randomUUID(), customer.getCustomerId());
    universe = Universe.saveDetails(universe.universeUUID, ApiUtils.mockUniverseUpdater());
    customer.addUniverseUUID(universe.universeUUID);
    customer.save();

    String method = "POST";
    String url = "/api/" + customer.uuid + "/universes/" + universe.universeUUID + "/tables";
    ObjectNode emptyJson = Json.newObject();
    String errorString = "{\"error\":\"ERROR executing DML bindLog[] error[NULL not allowed for " +
        "column \\\"TASK_UUID\\\"; SQL statement:\\\\n insert into customer_task (id, " +
        "customer_uuid, task_uuid, target_type, target_name, type, universe_uuid, create_time, " +
        "completion_time) values (?,?,?,?,?,?,?,?,?) [23502-191]]\"}";

    Result result = FakeApiHelper.doRequestWithAuthTokenAndBody(method, url, authToken, emptyJson);
    assertEquals(INTERNAL_SERVER_ERROR, result.status());
    assertThat(contentAsString(result), containsString(errorString));
  }

  @Test
  public void testCreateCassandraTableWithValidParams() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(Matchers.any(TaskInfo.Type.class),
        Matchers.any(TableDefinitionTaskParams.class))).thenReturn(fakeTaskUUID);

    Customer customer = Customer.create("Valid Customer", "abd@def.ghi", "password");
    String authToken = customer.createAuthToken();
    Universe universe = Universe.create("Universe-1", UUID.randomUUID(), customer.getCustomerId());
    universe = Universe.saveDetails(universe.universeUUID, ApiUtils.mockUniverseUpdater());
    customer.addUniverseUUID(universe.universeUUID);
    customer.save();

    String method = "POST";
    String url = "/api/" + customer.uuid + "/universes/" + universe.universeUUID + "/tables";
    JsonNode topJson = Json.parse(
        "{" +
          "\"cloud\":\"aws\"," +
          "\"universeUUID\":\"" + universe.universeUUID.toString() + "\"," +
          "\"expectedUniverseVersion\":-1," +
          "\"tableName\":\"test_table\"," +
          "\"tableType\":\"YSQL_TABLE_TYPE\"," +
          "\"tableDetails\":{" +
            "\"tableName\":\"test_table\"," +
            "\"columns\":[" +
              "{" +
                "\"columnOrder\":1," +
                "\"name\":\"k\"," +
                "\"type\":\"INT\"," +
                "\"isPartitionKey\":true," +
                "\"isClusteringKey\":false" +
              "},{" +
                "\"columnOrder\":2," +
                "\"name\":\"v\"," +
                "\"type\":\"VARCHAR\"," +
                "\"isPartitionKey\":false," +
                "\"isClusteringKey\":false" +
              "}" +
            "]" +
          "}" +
        "}");

    Result result = FakeApiHelper.doRequestWithAuthTokenAndBody(method, url, authToken, topJson);
    assertEquals(OK, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(json.get("taskUUID").asText(), fakeTaskUUID.toString());

    CustomerTask task = CustomerTask.find.where().eq("task_uuid", fakeTaskUUID).findUnique();
    assertNotNull(task);
    assertThat(task.getCustomerUUID(), allOf(notNullValue(), equalTo(customer.uuid)));
    assertThat(task.getTargetName(), allOf(notNullValue(), equalTo("test_table")));
    assertThat(task.getType(), allOf(notNullValue(), equalTo(CustomerTask.TaskType.Create)));
  }
}
