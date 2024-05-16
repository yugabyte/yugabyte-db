// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.commissioner.Common.CloudType.aws;
import static com.yugabyte.yw.common.AssertHelper.assertAuditEntry;
import static com.yugabyte.yw.common.AssertHelper.assertBadRequest;
import static com.yugabyte.yw.common.AssertHelper.assertErrorNodeValue;
import static com.yugabyte.yw.common.AssertHelper.assertForbidden;
import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static com.yugabyte.yw.common.AssertHelper.assertValue;
import static com.yugabyte.yw.common.ModelFactory.createFromConfig;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static com.yugabyte.yw.common.ModelFactory.generateTablespaceParams;
import static org.hamcrest.CoreMatchers.allOf;
import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.hamcrest.core.StringContains.containsString;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertThat;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyLong;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.doThrow;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.spy;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.core.type.TypeReference;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.ObjectReader;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableSet;
import com.google.common.net.HostAndPort;
import com.google.protobuf.ByteString;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.tasks.MultiTableBackup;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.TableSpaceStructures.PlacementBlock;
import com.yugabyte.yw.common.TableSpaceStructures.TableSpaceInfo;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.audit.AuditService;
import com.yugabyte.yw.common.customer.config.CustomerConfigService;
import com.yugabyte.yw.common.utils.FileUtils;
import com.yugabyte.yw.controllers.handlers.UniverseTableHandler;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.forms.BulkImportParams;
import com.yugabyte.yw.forms.CreateTablespaceParams;
import com.yugabyte.yw.forms.TableDefinitionTaskParams;
import com.yugabyte.yw.forms.TableInfoForm.TableInfoResp;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.extended.UserWithFeatures;
import com.yugabyte.yw.models.helpers.ColumnDetails;
import com.yugabyte.yw.models.helpers.TaskType;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashSet;
import java.util.Iterator;
import java.util.LinkedList;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.function.Function;
import java.util.stream.Collectors;
import junitparams.JUnitParamsRunner;
import org.junit.After;
import org.junit.Assert;
import org.junit.Before;
import org.junit.Ignore;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.ArgumentMatchers;
import org.mockito.MockedStatic;
import org.mockito.Mockito;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.ColumnSchema;
import org.yb.CommonTypes.TableType;
import org.yb.Schema;
import org.yb.Type;
import org.yb.client.ListTablesResponse;
import org.yb.client.YBClient;
import org.yb.master.MasterDdlOuterClass.ListTablesResponsePB.TableInfo;
import org.yb.master.MasterTypes;
import org.yb.master.MasterTypes.RelationType;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Result;

@RunWith(JUnitParamsRunner.class)
public class TablesControllerTest extends FakeDBApplication {
  public static final Logger LOG = LoggerFactory.getLogger(TablesControllerTest.class);

  @Rule public MockitoRule rule = MockitoJUnit.rule();

  private TablesController tablesController;
  private YBClient mockClient;
  private AuditService auditService;
  private ListTablesResponse mockListTablesResponse;
  MockedStatic<FileUtils> mockedFileUtils;
  private Customer customer;
  private Users user;
  private UniverseTableHandler tableHandler;

  private Schema getFakeSchema() {
    List<ColumnSchema> columnSchemas = new LinkedList<>();
    columnSchemas.add(
        new ColumnSchema.ColumnSchemaBuilder("mock_column", Type.INT32)
            .id(1)
            .hashKey(true)
            .build());
    return new Schema(columnSchemas);
  }

  @Before
  public void setUp() {
    mockClient = mock(YBClient.class);
    mockListTablesResponse = mock(ListTablesResponse.class);
    when(mockService.getClient(any(), any())).thenReturn(mockClient);
    tableHandler = spy(app.injector().instanceOf(UniverseTableHandler.class));

    auditService = new AuditService();
    Commissioner commissioner = app.injector().instanceOf(Commissioner.class);
    CustomerConfigService customerConfigService =
        app.injector().instanceOf(CustomerConfigService.class);
    tablesController =
        new TablesController(commissioner, mockService, customerConfigService, tableHandler);
    tablesController.setAuditService(auditService);

    mockedFileUtils = Mockito.mockStatic(FileUtils.class);
    mockedFileUtils.when(() -> FileUtils.readResource(anyString(), any())).thenReturn("QUERY");

    customer = ModelFactory.testCustomer();
    user = ModelFactory.testUser(customer);
  }

  @After
  public void cleanup() {
    mockedFileUtils.close();
  }

  @Test
  public void testListTablesFromYbClient() throws Exception {
    List<TableInfo> tableInfoList = new ArrayList<>();
    Set<String> tableNames = new HashSet<>();
    tableNames.add("Table1");
    tableNames.add("Table2");
    TableInfo ti1 =
        TableInfo.newBuilder()
            .setName("Table1")
            .setNamespace(MasterTypes.NamespaceIdentifierPB.newBuilder().setName("$$$Default"))
            .setId(ByteString.copyFromUtf8(UUID.randomUUID().toString().replace("-", "")))
            .setTableType(TableType.REDIS_TABLE_TYPE)
            .build();
    TableInfo ti2 =
        TableInfo.newBuilder()
            .setName("Table2")
            .setNamespace(MasterTypes.NamespaceIdentifierPB.newBuilder().setName("$$$Default"))
            .setId(ByteString.copyFromUtf8(UUID.randomUUID().toString().replace("-", "")))
            .setTableType(TableType.YQL_TABLE_TYPE)
            .build();
    // Create System type table, this will not be returned in response
    TableInfo ti3 =
        TableInfo.newBuilder()
            .setName("Table3")
            .setNamespace(MasterTypes.NamespaceIdentifierPB.newBuilder().setName("system"))
            .setId(ByteString.copyFromUtf8(UUID.randomUUID().toString()))
            .setTableType(TableType.YQL_TABLE_TYPE)
            .setRelationType(RelationType.SYSTEM_TABLE_RELATION)
            .build();
    tableInfoList.add(ti1);
    tableInfoList.add(ti2);
    tableInfoList.add(ti3);
    when(mockListTablesResponse.getTableInfoList()).thenReturn(tableInfoList);
    when(mockClient.getTablesList(null, false, null)).thenReturn(mockListTablesResponse);
    Universe u1 = createUniverse(customer.getId());
    u1 = Universe.saveDetails(u1.getUniverseUUID(), ApiUtils.mockUniverseUpdater());

    LOG.info("Created customer " + customer.getUuid() + " with universe " + u1.getUniverseUUID());
    Result r =
        tablesController.listTables(
            customer.getUuid(), u1.getUniverseUUID(), false, false, false, false); // modify mock
    JsonNode json = Json.parse(contentAsString(r));
    LOG.info("Fetched table list from universe, response: " + contentAsString(r));
    assertEquals(OK, r.status());
    assertTrue(json.isArray());
    Iterator<JsonNode> it = json.elements();
    int numTables = 0;
    while (it.hasNext()) {
      JsonNode table = it.next();
      String tableName = table.get("tableName").asText();
      String tableType = table.get("tableType").asText();
      String tableKeySpace = table.get("keySpace") != null ? table.get("keySpace").asText() : null;
      // Display table only if table is redis type or table is CQL type but not of system keyspace
      if (tableType.equals("REDIS_TABLE_TYPE")
          || (!tableKeySpace.toLowerCase().equals("system")
              && !tableKeySpace.toLowerCase().equals("system_schema")
              && !tableKeySpace.toLowerCase().equals("system_auth"))) {
        numTables++;
      }
      LOG.info("Table name: " + tableName + ", table type: " + tableType);
      assertTrue(tableNames.toString(), tableNames.contains(tableName));
      if (tableName.equals("Table1")) {
        assertEquals(TableType.REDIS_TABLE_TYPE.toString(), tableType);
        assertEquals("$$$Default", tableKeySpace);
      } else if (tableName.equals("Table2")) {
        assertEquals(TableType.YQL_TABLE_TYPE.toString(), tableType);
        assertEquals("$$$Default", tableKeySpace);
      }
      assertFalse(table.get("isIndexTable").asBoolean());
    }
    LOG.info("Processed " + numTables + " tables");
    assertEquals(numTables, tableNames.size());
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testUniverseListMastersNotQueryable() {
    Universe u1 = createUniverse("Universe-1", customer.getId());
    Result r =
        assertThrows(
                PlatformServiceException.class,
                () ->
                    tablesController.listTables(
                        customer.getUuid(),
                        u1.getUniverseUUID(),
                        false,
                        false,
                        false,
                        false)) // modify mock
            .buildResult(fakeRequest);
    assertEquals(503, r.status());
    assertEquals(
        "Expected error. Masters are not currently queryable.",
        Json.parse(contentAsString(r)).get("error").asText());
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testListTablesFromYbClientLeaderMasterNotAvailable() throws Exception {
    Universe u1 = createUniverse("Universe-1", customer.getId());
    u1 = Universe.saveDetails(u1.getUniverseUUID(), ApiUtils.mockUniverseUpdater());
    final Universe u2 = u1;
    doThrow(new RuntimeException("Timed out waiting for Master Leader after 10000 ms"))
        .when(mockClient)
        .waitForMasterLeader(anyLong());

    Result r =
        assertThrows(
                PlatformServiceException.class,
                () ->
                    tablesController.listTables(
                        customer.getUuid(),
                        u2.getUniverseUUID(),
                        false,
                        false,
                        false,
                        false)) // modify mock
            .buildResult(fakeRequest);
    assertEquals(500, r.status());
    assertEquals(
        "Could not find the master leader", Json.parse(contentAsString(r)).get("error").asText());
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testCreateCassandraTableWithInvalidUUID() {
    String authToken = user.createAuthToken();
    customer.save();

    UUID badUUID = UUID.randomUUID();
    String method = "POST";
    String url = "/api/customers/" + customer.getUuid() + "/universes/" + badUUID + "/tables";
    ObjectNode emptyJson = Json.newObject();

    Result r =
        assertPlatformException(
            () -> doRequestWithAuthTokenAndBody(method, url, authToken, emptyJson));
    assertEquals(BAD_REQUEST, r.status());
    String errMsg = "Cannot find universe " + badUUID;
    assertThat(Json.parse(contentAsString(r)).get("error").asText(), containsString(errMsg));
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testCreateCassandraTableWithInvalidParams() {
    String authToken = user.createAuthToken();
    Universe universe = createUniverse(customer.getId());
    universe = Universe.saveDetails(universe.getUniverseUUID(), ApiUtils.mockUniverseUpdater());

    String method = "POST";
    String url =
        "/api/customers/"
            + customer.getUuid()
            + "/universes/"
            + universe.getUniverseUUID()
            + "/tables";
    ObjectNode emptyJson = Json.newObject();
    String errorString = "Table details can not be null.";

    Result result =
        assertPlatformException(
            () -> doRequestWithAuthTokenAndBody(method, url, authToken, emptyJson));
    assertEquals(BAD_REQUEST, result.status());
    assertThat(contentAsString(result), containsString(errorString));
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testCreateCassandraTableWithValidParams() {
    UUID fakeTaskUUID = buildTaskInfo(null, TaskType.BackupTable);
    when(mockCommissioner.submit(
            ArgumentMatchers.any(TaskType.class),
            ArgumentMatchers.any(TableDefinitionTaskParams.class)))
        .thenReturn(fakeTaskUUID);
    String authToken = user.createAuthToken();
    Universe universe = createUniverse(customer.getId());
    universe = Universe.saveDetails(universe.getUniverseUUID(), ApiUtils.mockUniverseUpdater());

    String method = "POST";
    String url =
        "/api/customers/"
            + customer.getUuid()
            + "/universes/"
            + universe.getUniverseUUID()
            + "/tables";
    JsonNode topJson =
        Json.parse(
            "{"
                + "\"cloud\":\"aws\","
                + "\"universeUUID\":\""
                + universe.getUniverseUUID().toString()
                + "\","
                + "\"expectedUniverseVersion\":-1,"
                + "\"tableUUID\":null,"
                + "\"tableType\":\"YQL_TABLE_TYPE\","
                + "\"isIndexTable\":false,"
                + "\"tableDetails\":{"
                + "\"tableName\":\"test_table\","
                + "\"keyspace\":\"test_ks\","
                + "\"columns\":["
                + "{"
                + "\"columnOrder\":1,"
                + "\"name\":\"k\","
                + "\"type\":\"INT\","
                + "\"isPartitionKey\":true,"
                + "\"isClusteringKey\":false"
                + "},{"
                + "\"columnOrder\":2,"
                + "\"name\":\"v1\","
                + "\"type\":\"VARCHAR\","
                + "\"isPartitionKey\":false,"
                + "\"isClusteringKey\":false"
                + "},{"
                + "\"columnOrder\":3,"
                + "\"name\":\"v2\","
                + "\"type\":\"SET\","
                + "\"keyType\":\"INT\","
                + "\"isPartitionKey\":false,"
                + "\"isClusteringKey\":false"
                + "},{"
                + "\"columnOrder\":4,"
                + "\"name\":\"v3\","
                + "\"type\":\"MAP\","
                + "\"keyType\":\"UUID\","
                + "\"valueType\":\"VARCHAR\","
                + "\"isPartitionKey\":false,"
                + "\"isClusteringKey\":false"
                + "}"
                + "]"
                + "}"
                + "}");

    Result result = doRequestWithAuthTokenAndBody(method, url, authToken, topJson);
    assertEquals(OK, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(json.get("taskUUID").asText(), fakeTaskUUID.toString());

    CustomerTask task = CustomerTask.find.query().where().eq("task_uuid", fakeTaskUUID).findOne();
    assertNotNull(task);
    assertThat(task.getCustomerUUID(), allOf(notNullValue(), equalTo(customer.getUuid())));
    assertThat(task.getTargetName(), allOf(notNullValue(), equalTo("test_table")));
    assertThat(task.getType(), allOf(notNullValue(), equalTo(CustomerTask.TaskType.Create)));
    // TODO: Ideally i think the targetUUID for tables should be tableUUID, but currently
    // we don't control the UUID generation for tables from middleware side.
    assertThat(task.getTargetUUID(), allOf(notNullValue(), equalTo(universe.getUniverseUUID())));
    assertAuditEntry(1, customer.getUuid());
  }

  @Test
  public void testDescribeTableSuccess() throws Exception {
    when(mockClient.getTableSchemaByUUID(any(String.class))).thenReturn(mockSchemaResponse);

    // Creating a fake table
    Schema schema = getFakeSchema();
    UUID tableUUID = UUID.randomUUID();
    when(mockSchemaResponse.getSchema()).thenReturn(schema);
    when(mockSchemaResponse.getTableName()).thenReturn("mock_table");
    when(mockSchemaResponse.getNamespace()).thenReturn("mock_ks");
    when(mockSchemaResponse.getTableType()).thenReturn(TableType.YQL_TABLE_TYPE);
    when(mockSchemaResponse.getTableId()).thenReturn(tableUUID.toString().replace("-", ""));

    // Creating fake authentication
    Universe universe = createUniverse(customer.getId());
    universe = Universe.saveDetails(universe.getUniverseUUID(), ApiUtils.mockUniverseUpdater());

    Result result =
        tablesController.describe(
            customer.getUuid(), universe.getUniverseUUID(), tableUUID.toString());
    assertEquals(OK, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(tableUUID.toString(), json.get("tableUUID").asText());
    assertEquals("YQL_TABLE_TYPE", json.get("tableType").asText());
    assertEquals("mock_table", json.at("/tableDetails/tableName").asText());
    assertEquals("mock_ks", json.at("/tableDetails/keyspace").asText());
    assertEquals("mock_column", json.at("/tableDetails/columns/0/name").asText());
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testDescribeTableFailure() throws Exception {
    // Creating a fake table
    String mockTableUUID1 = UUID.randomUUID().toString().replace("-", "");
    UUID mockTableUUID2 = UUID.randomUUID();
    when(mockSchemaResponse.getTableId()).thenReturn(mockTableUUID1);
    when(mockClient.getTablesList()).thenReturn(mockListTablesResponse);
    when(mockClient.getTableSchemaByUUID(any(String.class))).thenReturn(mockSchemaResponse);

    // Creating fake authentication
    Universe universe = createUniverse(customer.getId());
    final Universe u =
        Universe.saveDetails(universe.getUniverseUUID(), ApiUtils.mockUniverseUpdater());

    Result result =
        assertPlatformException(
            () ->
                tablesController.describe(
                    customer.getUuid(), u.getUniverseUUID(), mockTableUUID2.toString()));
    assertEquals(BAD_REQUEST, result.status());
    // String errMsg = "Invalid Universe UUID: " + universe.universeUUID;
    String errMsg =
        "UUID of table in schema ("
            + mockTableUUID2.toString().replace("-", "")
            + ") did not match UUID of table in request ("
            + mockTableUUID1
            + ").";
    assertEquals(errMsg, Json.parse(contentAsString(result)).get("error").asText());
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testGetColumnTypes() {
    Result result = doRequest("GET", "/api/metadata/column_types");
    Set<ColumnDetails.YQLDataType> types = ImmutableSet.copyOf(ColumnDetails.YQLDataType.values());
    assertEquals(OK, result.status());
    JsonNode resultContent = Json.parse(contentAsString(result));
    assertThat(resultContent, notNullValue());
    JsonNode primitives = resultContent.get("primitives");
    JsonNode collections = resultContent.get("collections");
    Set<ColumnDetails.YQLDataType> resultTypes = new HashSet<>();

    // Check primitives
    for (int i = 0; i < primitives.size(); ++i) {
      String primitive = primitives.get(i).asText();
      ColumnDetails.YQLDataType type = ColumnDetails.YQLDataType.valueOf(primitive);
      assertFalse(type.isCollection());
      resultTypes.add(type);
    }

    // Check collections
    for (int i = 0; i < collections.size(); ++i) {
      String collection = collections.get(i).asText();
      ColumnDetails.YQLDataType type = ColumnDetails.YQLDataType.valueOf(collection);
      assertTrue(type.isCollection());
      resultTypes.add(type);
    }

    // Check all
    assertTrue(resultTypes.containsAll(types));
  }

  @Test
  public void testGetYQLDataTypes() throws IOException {
    Result result = doRequest("GET", "/api/metadata/yql_data_types");
    Set<ColumnDetails.YQLDataType> types = ImmutableSet.copyOf(ColumnDetails.YQLDataType.values());
    assertEquals(OK, result.status());

    JsonNode json = Json.parse(contentAsString(result));
    ObjectMapper mapper = new ObjectMapper();
    ObjectReader reader = mapper.readerFor(new TypeReference<List<ColumnDetails.YQLDataType>>() {});
    List<ColumnDetails.YQLDataType> yqlList = reader.readValue(json);

    // Check all
    assertTrue(yqlList.containsAll(types));
  }

  @Test
  public void testBulkImportWithValidParams() {
    UUID fakeTaskUUID = buildTaskInfo(null, TaskType.BackupTable);
    when(mockCommissioner.submit(
            ArgumentMatchers.any(TaskType.class), ArgumentMatchers.any(BulkImportParams.class)))
        .thenReturn(fakeTaskUUID);

    ModelFactory.awsProvider(customer);
    String authToken = user.createAuthToken();
    Universe universe = createUniverse(customer.getId());
    universe = Universe.saveDetails(universe.getUniverseUUID(), ApiUtils.mockUniverseUpdater(aws));

    String method = "PUT";
    String url =
        "/api/customers/"
            + customer.getUuid()
            + "/universes/"
            + universe.getUniverseUUID()
            + "/tables/"
            + UUID.randomUUID()
            + "/bulk_import";
    ObjectNode topJson = Json.newObject();
    topJson.put("s3Bucket", "s3://foo.bar.com/bulkload");
    topJson.put("keyspace", "mock_ks");
    topJson.put("tableName", "mock_table");

    Result result = doRequestWithAuthTokenAndBody(method, url, authToken, topJson);
    assertEquals(OK, result.status());
    assertAuditEntry(1, customer.getUuid());
  }

  @Test
  public void testBulkImportWithInvalidParams() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(
            ArgumentMatchers.any(TaskType.class), ArgumentMatchers.any(BulkImportParams.class)))
        .thenReturn(fakeTaskUUID);
    ModelFactory.awsProvider(customer);
    String authToken = user.createAuthToken();
    Universe universe = createUniverse(customer.getId());
    universe = Universe.saveDetails(universe.getUniverseUUID(), ApiUtils.mockUniverseUpdater(aws));

    String method = "PUT";
    String url =
        "/api/customers/"
            + customer.getUuid()
            + "/universes/"
            + universe.getUniverseUUID()
            + "/tables/"
            + UUID.randomUUID()
            + "/bulk_import";
    ObjectNode topJson = Json.newObject();
    topJson.put("s3Bucket", "foobar");
    topJson.put("keyspace", "mock_ks");
    topJson.put("tableName", "mock_table");

    Result result =
        assertPlatformException(
            () -> doRequestWithAuthTokenAndBody(method, url, authToken, topJson));
    assertEquals(BAD_REQUEST, result.status());
    assertThat(contentAsString(result), containsString("Invalid S3 Bucket provided: foobar"));
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testCreateBackupWithInvalidParams() {
    Universe universe = ModelFactory.createUniverse(customer.getId());
    String url =
        "/api/customers/"
            + customer.getUuid()
            + "/universes/"
            + universe.getUniverseUUID()
            + "/tables/"
            + UUID.randomUUID()
            + "/create_backup";
    ObjectNode bodyJson = Json.newObject();

    Result result =
        assertPlatformException(
            () -> doRequestWithAuthTokenAndBody("PUT", url, user.createAuthToken(), bodyJson));
    JsonNode resultJson = Json.parse(contentAsString(result));
    assertEquals(BAD_REQUEST, result.status());
    assertErrorNodeValue(resultJson, "storageConfigUUID", "This field is required");
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testCreateBackupWithInvalidStorageConfig() {
    Universe universe = ModelFactory.createUniverse(customer.getId());
    String url =
        "/api/customers/"
            + customer.getUuid()
            + "/universes/"
            + universe.getUniverseUUID()
            + "/tables/"
            + UUID.randomUUID()
            + "/create_backup";
    ObjectNode bodyJson = Json.newObject();
    UUID randomUUID = UUID.randomUUID();
    bodyJson.put("keyspace", "foo");
    bodyJson.put("tableName", "bar");
    bodyJson.put("actionType", "CREATE");
    bodyJson.put("storageConfigUUID", randomUUID.toString());

    Result result =
        assertPlatformException(
            () -> doRequestWithAuthTokenAndBody("PUT", url, user.createAuthToken(), bodyJson));
    assertBadRequest(result, "Invalid StorageConfig UUID: " + randomUUID);
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testCreateBackupWithReadOnlyUser() {
    user.delete();
    user = ModelFactory.testUser(customer, Users.Role.ReadOnly);
    Universe universe = ModelFactory.createUniverse(customer.getId());
    String url =
        "/api/customers/"
            + customer.getUuid()
            + "/universes/"
            + universe.getUniverseUUID()
            + "/tables/"
            + UUID.randomUUID()
            + "/create_backup";
    ObjectNode bodyJson = Json.newObject();
    UUID randomUUID = UUID.randomUUID();
    bodyJson.put("keyspace", "foo");
    bodyJson.put("tableName", "bar");
    bodyJson.put("actionType", "CREATE");
    bodyJson.put("storageConfigUUID", randomUUID.toString());

    Result result = doRequestWithAuthTokenAndBody("PUT", url, user.createAuthToken(), bodyJson);
    assertForbidden(result, "User doesn't have access");
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testCreateBackupWithBackupAdminUser() {
    user.delete();
    user = ModelFactory.testUser(customer, Users.Role.BackupAdmin);
    Universe universe = ModelFactory.createUniverse(customer.getId());
    UUID tableUUID = UUID.randomUUID();
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(customer, "TEST17");
    String url =
        "/api/customers/"
            + customer.getUuid()
            + "/universes/"
            + universe.getUniverseUUID()
            + "/tables/"
            + tableUUID
            + "/create_backup";
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("keyspace", "foo");
    bodyJson.put("tableName", "bar");
    bodyJson.put("actionType", "CREATE");
    bodyJson.put("storageConfigUUID", customerConfig.getConfigUUID().toString());

    ArgumentCaptor<TaskType> taskType = ArgumentCaptor.forClass(TaskType.class);

    ArgumentCaptor<BackupTableParams> taskParams = ArgumentCaptor.forClass(BackupTableParams.class);
    UUID fakeTaskUUID = buildTaskInfo(null, TaskType.BackupTable);
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    Result result = doRequestWithAuthTokenAndBody("PUT", url, user.createAuthToken(), bodyJson);
    verify(mockCommissioner, times(1)).submit(taskType.capture(), taskParams.capture());
    assertEquals(TaskType.BackupUniverse, taskType.getValue());
    assertOk(result);
    JsonNode resultJson = Json.parse(contentAsString(result));
    assertValue(resultJson, "taskUUID", fakeTaskUUID.toString());
    CustomerTask ct = CustomerTask.findByTaskUUID(fakeTaskUUID);
    assertNotNull(ct);
    assertAuditEntry(1, customer.getUuid());
  }

  @Test
  public void testCreateBackupWithValidParams() {
    Universe universe = ModelFactory.createUniverse(customer.getId());
    UUID tableUUID = UUID.randomUUID();
    String url =
        "/api/customers/"
            + customer.getUuid()
            + "/universes/"
            + universe.getUniverseUUID()
            + "/tables/"
            + tableUUID
            + "/create_backup";
    ObjectNode bodyJson = Json.newObject();
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(customer, "TEST18");
    bodyJson.put("keyspace", "foo");
    bodyJson.put("tableName", "bar");
    bodyJson.put("actionType", "CREATE");
    bodyJson.put("storageConfigUUID", customerConfig.getConfigUUID().toString());

    ArgumentCaptor<TaskType> taskType = ArgumentCaptor.forClass(TaskType.class);

    ArgumentCaptor<BackupTableParams> taskParams = ArgumentCaptor.forClass(BackupTableParams.class);

    UUID fakeTaskUUID = buildTaskInfo(null, TaskType.BackupTable);
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    Result result = doRequestWithAuthTokenAndBody("PUT", url, user.createAuthToken(), bodyJson);
    verify(mockCommissioner, times(1)).submit(taskType.capture(), taskParams.capture());
    assertEquals(TaskType.BackupUniverse, taskType.getValue());
    assertOk(result);
    JsonNode resultJson = Json.parse(contentAsString(result));
    assertValue(resultJson, "taskUUID", fakeTaskUUID.toString());
    CustomerTask ct = CustomerTask.findByTaskUUID(fakeTaskUUID);
    assertNotNull(ct);
    assertAuditEntry(1, customer.getUuid());
  }

  @Test
  @Ignore
  public void testCreateBackupOnDisabledTableFails() {
    Universe universe = createUniverse(customer.getId());
    final Universe u =
        Universe.saveDetails(universe.getUniverseUUID(), ApiUtils.mockUniverseUpdater());

    TablesController mockTablesController = spy(tablesController);

    doThrow(new PlatformServiceException(BAD_REQUEST, "bad request"))
        .when(mockTablesController)
        .validateTables(any(), any());

    UUID uuid = UUID.randomUUID();
    Result r =
        assertPlatformException(
            () ->
                mockTablesController.createBackup(
                    customer.getUuid(), u.getUniverseUUID(), uuid, fakeRequest));

    assertBadRequest(r, "bad request");
  }

  @Test
  public void testCreateBackupFailureInProgress() {
    UUID tableUUID = UUID.randomUUID();
    Universe universe = createUniverse(customer.getId());
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(), ApiUtils.mockUniverseUpdater("host", null));
    String url =
        "/api/customers/"
            + customer.getUuid()
            + "/universes/"
            + universe.getUniverseUUID()
            + "/tables/"
            + tableUUID
            + "/create_backup";
    ObjectNode bodyJson = Json.newObject();
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(customer, "TEST19");
    bodyJson.put("keyspace", "foo");
    bodyJson.put("tableName", "bar");
    bodyJson.put("actionType", "CREATE");
    bodyJson.put("storageConfigUUID", customerConfig.getConfigUUID().toString());

    Result result =
        assertPlatformException(
            () -> doRequestWithAuthTokenAndBody("PUT", url, user.createAuthToken(), bodyJson));

    String errMsg =
        String.format(
            "Cannot run Backup task since the " + "universe %s is currently in a locked state.",
            universe.getUniverseUUID().toString());
    assertBadRequest(result, errMsg);
  }

  @Test
  public void testCreateBackupCronExpression() {
    Universe universe = ModelFactory.createUniverse(customer.getId());
    UUID tableUUID = UUID.randomUUID();
    String url =
        "/api/customers/"
            + customer.getUuid()
            + "/universes/"
            + universe.getUniverseUUID()
            + "/tables/"
            + tableUUID
            + "/create_backup";
    ObjectNode bodyJson = Json.newObject();
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(customer, "TEST20");
    bodyJson.put("keyspace", "foo");
    bodyJson.put("tableName", "bar");
    bodyJson.put("actionType", "CREATE");
    bodyJson.put("storageConfigUUID", customerConfig.getConfigUUID().toString());
    bodyJson.put("cronExpression", "5 * * * *");
    Result result = doRequestWithAuthTokenAndBody("PUT", url, user.createAuthToken(), bodyJson);
    assertOk(result);
    JsonNode resultJson = Json.parse(contentAsString(result));
    UUID scheduleUUID = UUID.fromString(resultJson.path("scheduleUUID").asText());
    Schedule schedule = Schedule.getOrBadRequest(scheduleUUID);
    assertNotNull(schedule);
    assertEquals(schedule.getCronExpression(), "5 * * * *");
    assertAuditEntry(1, customer.getUuid());
  }

  @Test
  @Ignore
  public void testCreateMultiBackup() throws Exception {
    Universe universe = ModelFactory.createUniverse(customer.getId());
    String url =
        "/api/customers/"
            + customer.getUuid()
            + "/universes/"
            + universe.getUniverseUUID()
            + "/multi_table_backup";
    ObjectNode bodyJson = Json.newObject();
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(customer, "TEST21");
    bodyJson.put("actionType", "CREATE");
    bodyJson.put("storageConfigUUID", customerConfig.getConfigUUID().toString());

    ArgumentCaptor<TaskType> taskType = ArgumentCaptor.forClass(TaskType.class);

    ArgumentCaptor<MultiTableBackup.Params> taskParams =
        ArgumentCaptor.forClass(MultiTableBackup.Params.class);

    UUID fakeTaskUUID = UUID.randomUUID();

    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    Result result = doRequestWithAuthTokenAndBody("PUT", url, user.createAuthToken(), bodyJson);
    verify(mockCommissioner, times(1)).submit(taskType.capture(), taskParams.capture());
    assertEquals(TaskType.MultiTableBackup, taskType.getValue());
    assertOk(result);
    JsonNode resultJson = Json.parse(contentAsString(result));
    assertValue(resultJson, "taskUUID", fakeTaskUUID.toString());
    CustomerTask ct = CustomerTask.findByTaskUUID(fakeTaskUUID);
    assertNotNull(ct);
    assertAuditEntry(1, customer.getUuid());
  }

  @Test
  public void testCreateMultiBackupFailureInProgress() {
    Universe universe = createUniverse(customer.getId());
    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(), ApiUtils.mockUniverseUpdater("host", null));
    String url =
        "/api/customers/"
            + customer.getUuid()
            + "/universes/"
            + universe.getUniverseUUID()
            + "/multi_table_backup";
    ObjectNode bodyJson = Json.newObject();
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(customer, "TEST22");
    bodyJson.put("actionType", "CREATE");
    bodyJson.put("storageConfigUUID", customerConfig.getConfigUUID().toString());

    Result result =
        assertPlatformException(
            () -> doRequestWithAuthTokenAndBody("PUT", url, user.createAuthToken(), bodyJson));
    String errMsg =
        String.format(
            "Cannot run Backup task since the " + "universe %s is currently in a locked state.",
            universe.getUniverseUUID().toString());
    assertBadRequest(result, errMsg);
  }

  @Test
  public void testCreateMultiBackupScheduleCronNoTables() {
    Universe universe = ModelFactory.createUniverse(customer.getId());
    String url =
        "/api/customers/"
            + customer.getUuid()
            + "/universes/"
            + universe.getUniverseUUID()
            + "/multi_table_backup";
    ObjectNode bodyJson = Json.newObject();
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(customer, "TEST23");
    bodyJson.put("actionType", "CREATE");
    bodyJson.put("storageConfigUUID", customerConfig.getConfigUUID().toString());
    bodyJson.put("cronExpression", "5 * * * *");
    bodyJson.put("keyspace", "$$$Default");

    Result result =
        assertPlatformException(
            () -> doRequestWithAuthTokenAndBody("PUT", url, user.createAuthToken(), bodyJson));
    String errMsg = "Cannot initiate backup with empty Keyspace";
    assertBadRequest(result, errMsg);
  }

  @Test
  public void testCreateMultiBackupScheduleFrequencyEmptyKeyspace() {
    Universe universe = ModelFactory.createUniverse(customer.getId());
    String url =
        "/api/customers/"
            + customer.getUuid()
            + "/universes/"
            + universe.getUniverseUUID()
            + "/multi_table_backup";
    ObjectNode bodyJson = Json.newObject();
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(customer, "TEST24");
    bodyJson.put("actionType", "CREATE");
    bodyJson.put("storageConfigUUID", customerConfig.getConfigUUID().toString());
    bodyJson.put("schedulingFrequency", "6000");
    bodyJson.put("keyspace", "$$$Default");

    Result result =
        assertPlatformException(
            () -> doRequestWithAuthTokenAndBody("PUT", url, user.createAuthToken(), bodyJson));
    assertBadRequest(result, "Cannot initiate backup with empty Keyspace");
  }

  @Test
  public void testDeleteTableWithValidParams() throws Exception {
    Http.Request request =
        new Http.RequestBuilder().method("DELETE").path("/api/customer/test/universe/test").build();
    RequestContext.put(TokenAuthenticator.USER, new UserWithFeatures().setUser(user));
    tablesController.commissioner = mockCommissioner;
    UUID fakeTaskUUID = buildTaskInfo(null, TaskType.BackupTable);
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);

    // Creating a fake table
    Schema schema = getFakeSchema();
    UUID tableUUID = UUID.randomUUID();
    when(mockClient.getTableSchemaByUUID(eq(tableUUID.toString().replace("-", ""))))
        .thenReturn(mockSchemaResponse);
    when(mockSchemaResponse.getSchema()).thenReturn(schema);
    when(mockSchemaResponse.getTableName()).thenReturn("mock_table");
    when(mockSchemaResponse.getNamespace()).thenReturn("mock_ks");
    when(mockSchemaResponse.getTableType()).thenReturn(TableType.YQL_TABLE_TYPE);
    when(mockSchemaResponse.getTableId()).thenReturn(tableUUID.toString().replace("-", ""));

    Universe universe = createUniverse(customer.getId());
    universe = Universe.saveDetails(universe.getUniverseUUID(), ApiUtils.mockUniverseUpdater());

    Result result =
        tablesController.drop(customer.getUuid(), universe.getUniverseUUID(), tableUUID, request);
    assertEquals(OK, result.status());
    assertAuditEntry(1, customer.getUuid());
  }

  @Test
  public void testDeleteTableWithInvalidparams() {
    Universe universe = createUniverse(customer.getId());
    final Universe u =
        Universe.saveDetails(universe.getUniverseUUID(), ApiUtils.mockUniverseUpdater());

    UUID badTableUUID = UUID.randomUUID();
    String errorString = "No table for UUID: " + badTableUUID;

    Result result =
        assertPlatformException(
            () ->
                tablesController.drop(
                    customer.getUuid(), u.getUniverseUUID(), badTableUUID, fakeRequest));
    assertEquals(BAD_REQUEST, result.status());
    assertThat(contentAsString(result), containsString(errorString));
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testDisallowBackup() throws Exception {
    List<TableInfo> tableInfoList = new ArrayList<>();
    UUID table1Uuid = UUID.randomUUID();
    UUID table2Uuid = UUID.randomUUID();
    UUID indexUuid = UUID.randomUUID();
    UUID ysqlUuid = UUID.randomUUID();
    TableInfo ti1 =
        TableInfo.newBuilder()
            .setName("Table1")
            .setNamespace(MasterTypes.NamespaceIdentifierPB.newBuilder().setName("$$$Default"))
            .setId(ByteString.copyFromUtf8(table1Uuid.toString()))
            .setTableType(TableType.YQL_TABLE_TYPE)
            .build();
    TableInfo ti2 =
        TableInfo.newBuilder()
            .setName("Table2")
            .setNamespace(MasterTypes.NamespaceIdentifierPB.newBuilder().setName("$$$Default"))
            .setId(ByteString.copyFromUtf8(table2Uuid.toString()))
            .setTableType(TableType.YQL_TABLE_TYPE)
            .build();
    TableInfo ti3 =
        TableInfo.newBuilder()
            .setName("TableIndex")
            .setNamespace(MasterTypes.NamespaceIdentifierPB.newBuilder().setName("$$$Default"))
            .setId(ByteString.copyFromUtf8(indexUuid.toString()))
            .setTableType(TableType.YQL_TABLE_TYPE)
            .setRelationType(RelationType.INDEX_TABLE_RELATION)
            .build();
    TableInfo ti4 =
        TableInfo.newBuilder()
            .setName("TableYsql")
            .setNamespace(MasterTypes.NamespaceIdentifierPB.newBuilder().setName("$$$Default"))
            .setId(ByteString.copyFromUtf8(ysqlUuid.toString()))
            .setTableType(TableType.PGSQL_TABLE_TYPE)
            .build();

    tableInfoList.add(ti1);
    tableInfoList.add(ti2);
    tableInfoList.add(ti3);
    tableInfoList.add(ti4);

    when(mockListTablesResponse.getTableInfoList()).thenReturn(tableInfoList);
    when(mockClient.getTablesList()).thenReturn(mockListTablesResponse);
    Universe universe = mock(Universe.class);
    when(universe.getMasterAddresses()).thenReturn("fake_address");
    when(universe.getCertificateNodetoNode()).thenReturn("fake_certificate");

    // Disallow on Index Table.
    assertPlatformException(
        () ->
            tablesController.validateTables(
                Arrays.asList(table1Uuid, table2Uuid, indexUuid), universe));

    // Disallow on YSQL table.
    assertPlatformException(
        () ->
            tablesController.validateTables(
                Arrays.asList(table1Uuid, table2Uuid, ysqlUuid), universe));

    // Allow on YCQL tables and empty list.
    tablesController.validateTables(Arrays.asList(table1Uuid, table2Uuid), universe);

    tablesController.validateTables(new ArrayList<>(), universe);
  }

  @Test
  public void testListTableSpaces() throws Exception {
    Provider provider = ModelFactory.awsProvider(customer);
    Universe u1 = createFromConfig(provider, "Existing", "r1-az1-4-1;r1-az2-3-1;r1-az3-4-1");

    final String shellResponseString =
        TestUtils.readResource("com/yugabyte/yw/controllers/tablespaces_shell_response.txt");

    when(mockClient.getLeaderMasterHostAndPort())
        .thenReturn(HostAndPort.fromParts("1.1.1.1", 7000));
    ShellResponse shellResponse1 =
        ShellResponse.create(ShellResponse.ERROR_CODE_SUCCESS, shellResponseString);
    when(mockNodeUniverseManager.runYsqlCommand(any(), any(), anyString(), any()))
        .thenReturn(shellResponse1);

    Result r = tablesController.listTableSpaces(customer.getUuid(), u1.getUniverseUUID());
    assertEquals(OK, r.status());
    JsonNode json = Json.parse(contentAsString(r));
    ObjectMapper objectMapper = new ObjectMapper();

    List<TableSpaceInfo> tableSpaceInfoRespList =
        objectMapper.readValue(json.toString(), new TypeReference<List<TableSpaceInfo>>() {});
    assertNotNull(tableSpaceInfoRespList);
    assertEquals(4, tableSpaceInfoRespList.size());

    Map<String, TableSpaceInfo> tableSpacesMap =
        tableSpaceInfoRespList.stream().collect(Collectors.toMap(x -> x.name, Function.identity()));

    TableSpaceInfo ap_south_1_tablespace = tableSpacesMap.get("ap_south_1_tablespace");
    TableSpaceInfo us_west_2_tablespace = tableSpacesMap.get("us_west_2_tablespace");
    TableSpaceInfo us_west_1_tablespace = tableSpacesMap.get("us_west_1_tablespace");
    TableSpaceInfo us_west_3_tablespace = tableSpacesMap.get("us_west_3_tablespace");
    assertNotNull(ap_south_1_tablespace);
    assertNotNull(us_west_2_tablespace);
    assertNotNull(us_west_1_tablespace);
    assertNotNull(us_west_3_tablespace);

    assertEquals(3, ap_south_1_tablespace.numReplicas);
    assertEquals(3, ap_south_1_tablespace.placementBlocks.size());
    Map<String, PlacementBlock> ap_south_1_tablespace_zones =
        ap_south_1_tablespace.placementBlocks.stream()
            .collect(Collectors.toMap(x -> x.zone, Function.identity()));
    assertNotNull(ap_south_1_tablespace_zones.get("ap-south-1a"));
    assertEquals("ap-south-1", ap_south_1_tablespace_zones.get("ap-south-1a").region);
    assertEquals("aws", ap_south_1_tablespace_zones.get("ap-south-1a").cloud);
    assertEquals(1, ap_south_1_tablespace_zones.get("ap-south-1a").minNumReplicas);

    assertEquals(3, us_west_2_tablespace.numReplicas);
    assertEquals(3, us_west_2_tablespace.placementBlocks.size());
    assertEquals(1, us_west_1_tablespace.numReplicas);
    assertEquals(1, us_west_1_tablespace.placementBlocks.size());
    assertEquals(1, us_west_3_tablespace.numReplicas);
    assertEquals(1, us_west_3_tablespace.placementBlocks.size());
  }

  @Test
  public void testCreateTablespaces_HappyPath() {
    UUID fakeTaskUUID = buildTaskInfo(null, TaskType.BackupTable);
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);

    Provider provider = ModelFactory.awsProvider(customer);
    Universe universe = createFromConfig(provider, "Existing", "r1-az1-4-1;r1-az2-3-1;r1-az3-4-1");
    String authToken = user.createAuthToken();

    CreateTablespaceParams params =
        generateTablespaceParams(
            universe.getUniverseUUID(), provider.getCode(), 3, "r1-az1-1;r1-az2-1;r1-az3-1");
    Result result =
        doRequestWithAuthTokenAndBody(
            "POST",
            "/api/customers/"
                + customer.getUuid()
                + "/universes/"
                + universe.getUniverseUUID()
                + "/tablespaces",
            authToken,
            Json.toJson(params));

    assertOk(result);
  }

  @Test
  public void testCreateTablespaces_AnnotatedValidators_FailFlows() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);

    Provider provider = ModelFactory.awsProvider(customer);
    Universe universe = createFromConfig(provider, "Existing", "r1-az1-4-1;r1-az2-3-1;r1-az3-4-1");
    String authToken = user.createAuthToken();

    CreateTablespaceParams params =
        generateTablespaceParams(
            universe.getUniverseUUID(), provider.getCode(), 0, "r1-az1-1;r1-az2-1;r1-az3-1");
    assertCreateTableSpacesError(
        universe.getUniverseUUID(),
        authToken,
        params,
        "{\"tablespaceInfos[0].numReplicas\":[\"must be greater than or equal to 1\"]}");

    params = new CreateTablespaceParams();
    params.tablespaceInfos = new ArrayList<>();
    assertCreateTableSpacesError(
        universe.getUniverseUUID(),
        authToken,
        params,
        "{\"tablespaceInfos\":[\"size must be between 1 and 2147483647\"]}");

    params =
        generateTablespaceParams(
            universe.getUniverseUUID(), provider.getCode(), 3, "r1-az1-1;r1-az2-1;r1-az3-1");
    params.tablespaceInfos.get(0).name = "";
    assertCreateTableSpacesError(
        universe.getUniverseUUID(),
        authToken,
        params,
        "{\"tablespaceInfos[0].name\":[\"size must be between 1 and 2147483647\"]}");

    params = generateTablespaceParams(universe.getUniverseUUID(), provider.getCode(), 3, "");
    assertCreateTableSpacesError(
        universe.getUniverseUUID(),
        authToken,
        params,
        "{\"tablespaceInfos[0].placementBlocks\":[\"size must be between 1 and 2147483647\"]}");

    params =
        generateTablespaceParams(
            universe.getUniverseUUID(), provider.getCode(), 3, "r1-az1-1;r1-az2-1;r1-az3-1");
    params.tablespaceInfos.get(0).placementBlocks.get(0).cloud = "";
    assertCreateTableSpacesError(
        universe.getUniverseUUID(),
        authToken,
        params,
        "{\"tablespaceInfos[0].placementBlocks[0].cloud\":[\"size must be between"
            + " 1 and 2147483647\"]}");
  }

  private void assertCreateTableSpacesError(
      UUID universeUUID, String authToken, CreateTablespaceParams params, String error) {
    Result result =
        assertPlatformException(
            () ->
                doRequestWithAuthTokenAndBody(
                    "POST",
                    "/api/customers/"
                        + customer.getUuid()
                        + "/universes/"
                        + universeUUID
                        + "/tablespaces",
                    authToken,
                    Json.toJson(params)));
    assertBadRequest(result, error);
  }

  @Test
  public void testListTablesWithPartitionInfo() throws Exception {
    List<TableInfo> tableInfoList = new ArrayList<>();
    Set<String> tableNames = new HashSet<>();
    TableInfo ti1 =
        TableInfo.newBuilder()
            .setName("bank_transactions")
            .setNamespace(MasterTypes.NamespaceIdentifierPB.newBuilder().setName("$$$Default"))
            .setId(ByteString.copyFromUtf8(UUID.randomUUID().toString().replace("-", "")))
            .setTableType(TableType.YQL_TABLE_TYPE)
            .build();
    TableInfo ti2 =
        TableInfo.newBuilder()
            .setName("bank_transactions_india")
            .setNamespace(MasterTypes.NamespaceIdentifierPB.newBuilder().setName("$$$Default"))
            .setId(ByteString.copyFromUtf8(UUID.randomUUID().toString().replace("-", "")))
            .setTableType(TableType.YQL_TABLE_TYPE)
            .build();
    TableInfo ti3 =
        TableInfo.newBuilder()
            .setName("bank_transactions_eu")
            .setNamespace(MasterTypes.NamespaceIdentifierPB.newBuilder().setName("$$$Default"))
            .setId(ByteString.copyFromUtf8(UUID.randomUUID().toString().replace("-", "")))
            .setTableType(TableType.YQL_TABLE_TYPE)
            .build();
    // Create System type table, this will not be returned in response
    TableInfo ti4 =
        TableInfo.newBuilder()
            .setName("Table2")
            .setNamespace(MasterTypes.NamespaceIdentifierPB.newBuilder().setName("system"))
            .setId(ByteString.copyFromUtf8(UUID.randomUUID().toString().replace("-", "")))
            .setTableType(TableType.YQL_TABLE_TYPE)
            .setRelationType(RelationType.SYSTEM_TABLE_RELATION)
            .build();

    tableInfoList.add(ti1);
    tableInfoList.add(ti2);
    tableInfoList.add(ti3);
    tableInfoList.add(ti4);

    tableNames.add("bank_transactions");
    tableNames.add("bank_transactions_india");
    tableNames.add("bank_transactions_eu");
    tableNames.add("Table2");

    when(mockListTablesResponse.getTableInfoList()).thenReturn(tableInfoList);
    when(mockClient.getTablesList(null, false, null)).thenReturn(mockListTablesResponse);

    Customer customer = ModelFactory.testCustomer();
    Universe u1 = createUniverse(customer.getId());
    u1 = Universe.saveDetails(u1.getUniverseUUID(), ApiUtils.mockUniverseUpdater());

    ShellResponse shellResponse =
        ShellResponse.create(
            ShellResponse.ERROR_CODE_SUCCESS,
            TestUtils.readResource(
                "com/yugabyte/yw/controllers/table_partitions_shell_response.txt"));
    when(mockNodeUniverseManager.runYsqlCommand(any(), any(), eq("$$$Default"), any()))
        .thenReturn(shellResponse);
    when(mockNodeUniverseManager.runYsqlCommand(any(), any(), eq("system"), any()))
        .thenReturn(ShellResponse.create(ShellResponse.ERROR_CODE_SUCCESS, ""));

    LOG.info("Created customer " + customer.getUuid() + " with universe " + u1.getUniverseUUID());
    Result r =
        tablesController.listTables(
            customer.getUuid(), u1.getUniverseUUID(), true, false, false, false);
    JsonNode json = Json.parse(contentAsString(r));

    ObjectMapper objectMapper = new ObjectMapper();
    LOG.debug("JSON respone {}", json.toString());
    List<TableInfoResp> tableInfoRespList =
        objectMapper.readValue(json.toString(), new TypeReference<List<TableInfoResp>>() {});
    LOG.debug("Fetched table list from universe, response: " + contentAsString(r));
    assertEquals(OK, r.status());
    Assert.assertEquals(3, tableInfoRespList.size());

    Assert.assertEquals(
        Util.getUUIDRepresentation(ti1.getId().toStringUtf8()),
        tableInfoRespList.stream()
            .filter(x -> x.tableName.equals("bank_transactions_india"))
            .findAny()
            .get()
            .parentTableUUID);
    Assert.assertEquals(
        Util.getUUIDRepresentation(ti1.getId().toStringUtf8()),
        tableInfoRespList.stream()
            .filter(x -> x.tableName.equals("bank_transactions_eu"))
            .findAny()
            .get()
            .parentTableUUID);
    Assert.assertEquals(
        null,
        tableInfoRespList.stream()
            .filter(x -> x.tableName.equals("bank_transactions"))
            .findAny()
            .get()
            .parentTableUUID);
  }

  @Test
  public void testListTablesWithPartitionInfoMultipleDB() throws Exception {
    List<TableInfo> tableInfoList = new ArrayList<>();
    Set<String> tableNames = new HashSet<>();
    TableInfo ti1 =
        TableInfo.newBuilder()
            .setName("db1.table1")
            .setNamespace(MasterTypes.NamespaceIdentifierPB.newBuilder().setName("db1"))
            .setId(ByteString.copyFromUtf8(UUID.randomUUID().toString().replace("-", "")))
            .setTableType(TableType.YQL_TABLE_TYPE)
            .build();
    TableInfo ti2 =
        TableInfo.newBuilder()
            .setName("db1.table1.partition1")
            .setNamespace(MasterTypes.NamespaceIdentifierPB.newBuilder().setName("db1"))
            .setId(ByteString.copyFromUtf8(UUID.randomUUID().toString().replace("-", "")))
            .setTableType(TableType.YQL_TABLE_TYPE)
            .build();
    TableInfo ti3 =
        TableInfo.newBuilder()
            .setName("db1.table1.partition2")
            .setNamespace(MasterTypes.NamespaceIdentifierPB.newBuilder().setName("db1"))
            .setId(ByteString.copyFromUtf8(UUID.randomUUID().toString().replace("-", "")))
            .setTableType(TableType.YQL_TABLE_TYPE)
            .build();

    TableInfo ti4 =
        TableInfo.newBuilder()
            .setName("db2.table1")
            .setNamespace(MasterTypes.NamespaceIdentifierPB.newBuilder().setName("db2"))
            .setId(ByteString.copyFromUtf8(UUID.randomUUID().toString().replace("-", "")))
            .setTableType(TableType.YQL_TABLE_TYPE)
            .build();
    TableInfo ti5 =
        TableInfo.newBuilder()
            .setName("db2.table1.partition1")
            .setNamespace(MasterTypes.NamespaceIdentifierPB.newBuilder().setName("db2"))
            .setId(ByteString.copyFromUtf8(UUID.randomUUID().toString().replace("-", "")))
            .setTableType(TableType.YQL_TABLE_TYPE)
            .build();
    TableInfo ti6 =
        TableInfo.newBuilder()
            .setName("db2.table2")
            .setNamespace(MasterTypes.NamespaceIdentifierPB.newBuilder().setName("db2"))
            .setId(ByteString.copyFromUtf8(UUID.randomUUID().toString().replace("-", "")))
            .setTableType(TableType.YQL_TABLE_TYPE)
            .build();
    TableInfo ti7 =
        TableInfo.newBuilder()
            .setName("db3.table1")
            .setNamespace(MasterTypes.NamespaceIdentifierPB.newBuilder().setName("db3"))
            .setId(ByteString.copyFromUtf8(UUID.randomUUID().toString().replace("-", "")))
            .setTableType(TableType.YQL_TABLE_TYPE)
            .build();

    // Create System type table, this will not be returned in response
    TableInfo ti8 =
        TableInfo.newBuilder()
            .setName("Table1")
            .setNamespace(MasterTypes.NamespaceIdentifierPB.newBuilder().setName("system"))
            .setId(ByteString.copyFromUtf8(UUID.randomUUID().toString().replace("-", "")))
            .setTableType(TableType.YQL_TABLE_TYPE)
            .setRelationType(RelationType.SYSTEM_TABLE_RELATION)
            .build();

    tableInfoList.add(ti1);
    tableInfoList.add(ti2);
    tableInfoList.add(ti3);
    tableInfoList.add(ti4);
    tableInfoList.add(ti5);
    tableInfoList.add(ti6);
    tableInfoList.add(ti7);
    tableInfoList.add(ti8);

    tableNames.add("db1.table1");
    tableNames.add("db1.table1.partition1");
    tableNames.add("db1.table1.partition2");
    tableNames.add("db2.table1");
    tableNames.add("db2.table1.partition1");
    tableNames.add("db2.table2");
    tableNames.add("db3.table1");
    tableNames.add("Table1");

    when(mockListTablesResponse.getTableInfoList()).thenReturn(tableInfoList);
    when(mockClient.getTablesList(null, false, null)).thenReturn(mockListTablesResponse);
    Customer customer = ModelFactory.testCustomer();
    Universe u1 = createUniverse(customer.getId());
    u1 = Universe.saveDetails(u1.getUniverseUUID(), ApiUtils.mockUniverseUpdater());

    ShellResponse shellResponse1 =
        ShellResponse.create(
            ShellResponse.ERROR_CODE_SUCCESS,
            TestUtils.readResource(
                "com/yugabyte/yw/controllers/table_partitions_db1_shell_response.txt"));
    ShellResponse shellResponse2 =
        ShellResponse.create(
            ShellResponse.ERROR_CODE_SUCCESS,
            TestUtils.readResource(
                "com/yugabyte/yw/controllers/table_partitions_db2_shell_response.txt"));
    when(mockNodeUniverseManager.runYsqlCommand(any(), any(), eq("db1"), any()))
        .thenReturn(shellResponse1);
    when(mockNodeUniverseManager.runYsqlCommand(any(), any(), eq("db2"), any()))
        .thenReturn(shellResponse2);
    when(mockNodeUniverseManager.runYsqlCommand(any(), any(), eq("db3"), any()))
        .thenReturn(ShellResponse.create(ShellResponse.ERROR_CODE_SUCCESS, ""));
    when(mockNodeUniverseManager.runYsqlCommand(any(), any(), eq("system"), any()))
        .thenReturn(ShellResponse.create(ShellResponse.ERROR_CODE_SUCCESS, ""));

    LOG.info("Created customer " + customer.getUuid() + " with universe " + u1.getUniverseUUID());
    Result r =
        tablesController.listTables(
            customer.getUuid(), u1.getUniverseUUID(), true, false, false, false); // modify mock
    JsonNode json = Json.parse(contentAsString(r));

    ObjectMapper objectMapper = new ObjectMapper();
    List<TableInfoResp> tableInfoRespList =
        objectMapper.readValue(json.toString(), new TypeReference<List<TableInfoResp>>() {});
    assertEquals(OK, r.status());
    Assert.assertEquals(7, tableInfoRespList.size());

    List<TableInfoResp> db1 =
        tableInfoRespList.stream()
            .filter(x -> "db1".equals(x.keySpace))
            .collect(Collectors.toList());

    Assert.assertEquals(3, db1.size());
    Assert.assertEquals(
        Util.getUUIDRepresentation(ti1.getId().toStringUtf8()),
        db1.stream()
            .filter(x -> x.tableName.equals("db1.table1.partition1"))
            .findAny()
            .get()
            .parentTableUUID);
    Assert.assertEquals(
        Util.getUUIDRepresentation(ti1.getId().toStringUtf8()),
        db1.stream()
            .filter(x -> x.tableName.equals("db1.table1.partition2"))
            .findAny()
            .get()
            .parentTableUUID);
    Assert.assertEquals(
        null,
        db1.stream().filter(x -> x.tableName.equals("db1.table1")).findAny().get().parentTableUUID);

    List<TableInfoResp> db2 =
        tableInfoRespList.stream()
            .filter(x -> "db2".equals(x.keySpace))
            .collect(Collectors.toList());
    Assert.assertEquals(3, db2.size());
    Assert.assertEquals(
        Util.getUUIDRepresentation(ti4.getId().toStringUtf8()),
        db2.stream()
            .filter(x -> x.tableName.equals("db2.table1.partition1"))
            .findAny()
            .get()
            .parentTableUUID);
    Assert.assertEquals(
        null,
        db2.stream().filter(x -> x.tableName.equals("db2.table1")).findAny().get().parentTableUUID);

    List<TableInfoResp> db3 =
        tableInfoRespList.stream()
            .filter(x -> "db3".equals(x.keySpace))
            .collect(Collectors.toList());
    Assert.assertEquals(1, db3.size());
    Assert.assertEquals("db3.table1", db3.get(0).tableName);
  }

  @Test
  public void testExcludeColocatedTables() throws Exception {
    List<TableInfo> tableInfoList = tableInfoListWithColocated();

    when(mockListTablesResponse.getTableInfoList()).thenReturn(tableInfoList);
    when(mockClient.getTablesList(null, false, null)).thenReturn(mockListTablesResponse);
    Universe u1 = createUniverse(customer.getId());
    u1 = Universe.saveDetails(u1.getUniverseUUID(), ApiUtils.mockUniverseUpdater());

    LOG.info("Created customer " + customer.getUuid() + " with universe " + u1.getUniverseUUID());
    Result r =
        tablesController.listTables(
            customer.getUuid(), u1.getUniverseUUID(), false, true, false, false);
    JsonNode json = Json.parse(contentAsString(r));
    LOG.info("Fetched table list from universe, response: " + contentAsString(r));
    assertEquals(OK, r.status());
    assertTrue(json.isArray());
    Iterator<JsonNode> it = json.elements();
    int numTables = 0;
    while (it.hasNext()) {
      JsonNode table = it.next();
      String tableName = table.get("tableName").asText();
      String relationType = table.get("relationType").asText();
      String keySpace = table.get("keySpace").asText();
      if (tableName.equals("company")) {
        assertEquals(RelationType.USER_TABLE_RELATION.toString(), relationType);
      } else if (tableName.equals("company_name_idx")) {
        assertEquals(RelationType.INDEX_TABLE_RELATION.toString(), relationType);
      }
      // No system tables, colocated parent tables, or tables in db where colocation=true should be
      // displayed.
      assertEquals("db1", keySpace);
      assertNotEquals(RelationType.COLOCATED_PARENT_TABLE_RELATION.toString(), relationType);
      assertNotEquals(RelationType.SYSTEM_TABLE_RELATION.toString(), relationType);
      numTables++;
    }
    LOG.info("Processed " + numTables + " tables");
    assertEquals(tableInfoList.size() - 5, numTables);
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testIncludeColocatedParentTablesFalse() throws Exception {
    List<TableInfo> tableInfoList = tableInfoListWithColocated();

    when(mockListTablesResponse.getTableInfoList()).thenReturn(tableInfoList);
    when(mockClient.getTablesList(null, false, null)).thenReturn(mockListTablesResponse);
    Universe u1 = createUniverse(customer.getId());
    u1 = Universe.saveDetails(u1.getUniverseUUID(), ApiUtils.mockUniverseUpdater());

    LOG.info("Created customer " + customer.getUuid() + " with universe " + u1.getUniverseUUID());
    Result r =
        tablesController.listTables(
            customer.getUuid(), u1.getUniverseUUID(), false, false, false, false);
    JsonNode json = Json.parse(contentAsString(r));
    LOG.info("Fetched table list from universe, response: " + contentAsString(r));
    assertEquals(OK, r.status());
    assertTrue(json.isArray());
    Iterator<JsonNode> it = json.elements();
    int numTables = 0;
    while (it.hasNext()) {
      JsonNode table = it.next();
      String tableName = table.get("tableName").asText();
      String relationType = table.get("relationType").asText();
      String keySpace = table.get("keySpace").asText();

      switch (tableName) {
        case "house":
          assertEquals(RelationType.USER_TABLE_RELATION.toString(), relationType);
          assertEquals("db-col-old", keySpace);
          break;
        case "house_name_idx":
          assertEquals(RelationType.INDEX_TABLE_RELATION.toString(), relationType);
          assertEquals("db-col-old", keySpace);
          break;
        case "people":
          assertEquals(RelationType.USER_TABLE_RELATION.toString(), relationType);
          assertEquals("db-col-new", keySpace);
          break;
        case "company":
          assertEquals(RelationType.USER_TABLE_RELATION.toString(), relationType);
          assertEquals("db1", keySpace);
          break;
        case "company_name_idx":
          assertEquals(RelationType.INDEX_TABLE_RELATION.toString(), relationType);
          assertEquals("db1", keySpace);
          break;
        default:
          fail("Unexpected table name " + tableName);
      }

      // No system tables or colocation parent tables should be displayed.
      assertNotEquals(RelationType.SYSTEM_TABLE_RELATION.toString(), relationType);
      assertNotEquals(RelationType.COLOCATED_PARENT_TABLE_RELATION.toString(), relationType);
      numTables++;
    }
    LOG.info("Processed " + numTables + " tables");
    assertEquals(tableInfoList.size() - 2, numTables);
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testIncludeColocatedParentTablesTrue() throws Exception {
    List<TableInfo> tableInfoList = tableInfoListWithColocated();

    when(mockListTablesResponse.getTableInfoList()).thenReturn(tableInfoList);
    when(mockClient.getTablesList(null, false, null)).thenReturn(mockListTablesResponse);
    Universe u1 = createUniverse(customer.getId());
    u1 = Universe.saveDetails(u1.getUniverseUUID(), ApiUtils.mockUniverseUpdater());

    LOG.info("Created customer " + customer.getUuid() + " with universe " + u1.getUniverseUUID());
    Result r =
        tablesController.listTables(
            customer.getUuid(), u1.getUniverseUUID(), false, false, true, false);
    JsonNode json = Json.parse(contentAsString(r));
    LOG.info("Fetched table list from universe, response: " + contentAsString(r));
    assertEquals(OK, r.status());
    assertTrue(json.isArray());
    Iterator<JsonNode> it = json.elements();
    int numTables = 0;
    while (it.hasNext()) {
      JsonNode table = it.next();
      String tableName = table.get("tableName").asText();
      String relationType = table.get("relationType").asText();
      String keySpace = table.get("keySpace").asText();

      switch (tableName) {
        case "0000401b000030008000000000000000.colocated.parent.tablename":
          assertEquals(RelationType.COLOCATED_PARENT_TABLE_RELATION.toString(), relationType);
          assertEquals("db-col-old", keySpace);
          break;
        case "house":
          assertEquals(RelationType.USER_TABLE_RELATION.toString(), relationType);
          assertEquals("db-col-old", keySpace);
          break;
        case "house_name_idx":
          assertEquals(RelationType.INDEX_TABLE_RELATION.toString(), relationType);
          assertEquals("db-col-old", keySpace);
          break;
        case "0000203c000030008000000000000000.colocation.parent.tablename":
          assertEquals(RelationType.COLOCATED_PARENT_TABLE_RELATION.toString(), relationType);
          assertEquals("db-col-new", keySpace);
          break;
        case "people":
          assertEquals(RelationType.USER_TABLE_RELATION.toString(), relationType);
          assertEquals("db-col-new", keySpace);
          break;
        case "company":
          assertEquals(RelationType.USER_TABLE_RELATION.toString(), relationType);
          assertEquals("db1", keySpace);
          break;
        case "company_name_idx":
          assertEquals(RelationType.INDEX_TABLE_RELATION.toString(), relationType);
          assertEquals("db1", keySpace);
          break;
        default:
          fail("Unexpected table name " + tableName);
      }
      // No system tables should be displayed.
      assertNotEquals(RelationType.SYSTEM_TABLE_RELATION.toString(), relationType);
      numTables++;
    }
    LOG.info("Processed " + numTables + " tables");
    assertEquals(tableInfoList.size(), numTables);
    assertAuditEntry(0, customer.getUuid());
  }

  // When xClusterSupportedOnly flag is set as true and listTablesInfo rpc contains
  // indexed_table_id field, then no further RPC calls should be made to fetch table info.
  @Test
  public void testXClusterOnlyListTablesWithIndexedTable() throws Exception {
    List<TableInfo> mockTableInfoList = getTableInfoWithIndexTables(true, true);
    when(mockListTablesResponse.getTableInfoList()).thenReturn(mockTableInfoList);
    when(mockClient.getTablesList(null, false, null)).thenReturn(mockListTablesResponse);
    Universe u1 = createUniverse(customer.getId());
    u1 = Universe.saveDetails(u1.getUniverseUUID(), ApiUtils.mockUniverseUpdater());
    Universe.saveDetails(
        u1.getUniverseUUID(),
        universe -> {
          UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
          universeDetails.getPrimaryCluster().userIntent.ybSoftwareVersion = "2.21.1.0-b168";
          universe.setUniverseDetails(universeDetails);
        });
    u1 = Universe.getOrBadRequest(u1.getUniverseUUID());
    when(mockClient.getTableSchemaByUUID(anyString()))
        .thenThrow(new RuntimeException("Additional RPC calls made to getTableSchemaByUUID"));

    Result r =
        tablesController.listTables(
            customer.getUuid(), u1.getUniverseUUID(), false, false, true, true);
    JsonNode json = Json.parse(contentAsString(r));
    assertEquals(OK, r.status());
    assertTrue(json.isArray());
    assertEquals(2, json.size());
    Iterator<JsonNode> it = json.elements();
    JsonNode mainTable = it.next();
    JsonNode indexedTable = it.next();
    assertEquals(mainTable.get("tableUUID"), indexedTable.get("mainTableUUID"));
  }

  // When xClusterSupportedOnly flag is set as true and listTablesInfo rpc doesn't contain
  // indexed_table_id field, but there are no Index tables in the list tables response,
  // then no further RPC calls should be made to fetch table info.
  @Test
  public void testXClusterOnlyListTablesWithoutIndexedTableFieldWithoutIndexTables()
      throws Exception {
    List<TableInfo> mockTableInfoList = getTableInfoWithIndexTables(false, false);
    when(mockListTablesResponse.getTableInfoList()).thenReturn(mockTableInfoList);
    when(mockClient.getTablesList(null, false, null)).thenReturn(mockListTablesResponse);
    Universe u1 = createUniverse(customer.getId());
    u1 = Universe.saveDetails(u1.getUniverseUUID(), ApiUtils.mockUniverseUpdater());
    when(mockClient.getTableSchemaByUUID(anyString()))
        .thenThrow(new RuntimeException("Additional RPC calls made to getTableSchemaByUUID"));

    Result r =
        tablesController.listTables(
            customer.getUuid(), u1.getUniverseUUID(), false, false, true, true);
    JsonNode json = Json.parse(contentAsString(r));
    assertEquals(OK, r.status());
    assertTrue(json.isArray());
    assertEquals(1, json.size());
    Iterator<JsonNode> it = json.elements();
    JsonNode mainTable = it.next();
    assertEquals("main_table", mainTable.get("tableName").asText());
  }

  private List<TableInfo> tableInfoListWithColocated() {
    List<TableInfo> tableInfoList = new ArrayList<>();
    Set<String> tableNames = new HashSet<>();

    // Colocated db old naming style.
    ByteString keyspaceOldColocatedId = ByteString.copyFromUtf8("0000401b000030008000000000000000");
    String keyspaceOldColocatedName = "db-col-old";
    TableInfo ti1 =
        TableInfo.newBuilder()
            .setName("0000401b000030008000000000000000.colocated.parent.tablename")
            .setNamespace(
                MasterTypes.NamespaceIdentifierPB.newBuilder()
                    .setName(keyspaceOldColocatedName)
                    .setId(keyspaceOldColocatedId))
            .setId(
                ByteString.copyFromUtf8("0000401b000030008000000000000000.colocated.parent.uuid"))
            .setTableType(TableType.YQL_TABLE_TYPE)
            .setRelationType(RelationType.COLOCATED_PARENT_TABLE_RELATION)
            .build();
    TableInfo ti2 =
        TableInfo.newBuilder()
            .setName("house")
            .setNamespace(
                MasterTypes.NamespaceIdentifierPB.newBuilder()
                    .setName(keyspaceOldColocatedName)
                    .setId(keyspaceOldColocatedId))
            .setId(ByteString.copyFromUtf8("0000401b00003000800000000000401c"))
            .setTableType(TableType.YQL_TABLE_TYPE)
            .setRelationType(RelationType.USER_TABLE_RELATION)
            .build();
    // Create index.
    TableInfo ti3 =
        TableInfo.newBuilder()
            .setName("house_name_idx")
            .setNamespace(
                MasterTypes.NamespaceIdentifierPB.newBuilder()
                    .setName(keyspaceOldColocatedName)
                    .setId(keyspaceOldColocatedId))
            .setId(ByteString.copyFromUtf8("0000401b000030008000000000004029"))
            .setTableType(TableType.YQL_TABLE_TYPE)
            .setRelationType(RelationType.INDEX_TABLE_RELATION)
            .build();

    // Colocated db new naming style.
    ByteString keyspaceNewColocatedId = ByteString.copyFromUtf8("0000203c000030008000000000000000");
    String keyspaceNewColocatedName = "db-col-new";
    TableInfo ti4 =
        TableInfo.newBuilder()
            .setName("0000203c000030008000000000000000.colocation.parent.tablename")
            .setNamespace(
                MasterTypes.NamespaceIdentifierPB.newBuilder()
                    .setName(keyspaceNewColocatedName)
                    .setId(keyspaceNewColocatedId))
            .setId(
                ByteString.copyFromUtf8("0000203c000030008000000000000000.colocation.parent.uuid"))
            .setTableType(TableType.YQL_TABLE_TYPE)
            .setRelationType(RelationType.COLOCATED_PARENT_TABLE_RELATION)
            .build();
    TableInfo ti5 =
        TableInfo.newBuilder()
            .setName("people")
            .setNamespace(
                MasterTypes.NamespaceIdentifierPB.newBuilder()
                    .setName(keyspaceNewColocatedName)
                    .setId(keyspaceNewColocatedId))
            .setId(ByteString.copyFromUtf8("0000203c00003000800000000000502d"))
            .setTableType(TableType.YQL_TABLE_TYPE)
            .setRelationType(RelationType.USER_TABLE_RELATION)
            .build();

    // Database/keyspace with colocation=false.
    String keyspaceNonColocatedName = "db1";
    ByteString keyspaceNonColocatedId = ByteString.copyFromUtf8("000033e8000030008000000000000000");
    TableInfo ti6 =
        TableInfo.newBuilder()
            .setName("company")
            .setNamespace(
                MasterTypes.NamespaceIdentifierPB.newBuilder()
                    .setName(keyspaceNonColocatedName)
                    .setId(keyspaceNonColocatedId))
            .setId(ByteString.copyFromUtf8("000033e8000030008000000000004005"))
            .setTableType(TableType.YQL_TABLE_TYPE)
            .setRelationType(RelationType.USER_TABLE_RELATION)
            .build();
    TableInfo ti7 =
        TableInfo.newBuilder()
            .setName("company_name_idx")
            .setNamespace(
                MasterTypes.NamespaceIdentifierPB.newBuilder()
                    .setName(keyspaceNonColocatedName)
                    .setId(keyspaceNonColocatedId))
            .setId(ByteString.copyFromUtf8("000033e800003000800000000000400f"))
            .setTableType(TableType.YQL_TABLE_TYPE)
            .setRelationType(RelationType.INDEX_TABLE_RELATION)
            .build();

    tableInfoList.add(ti1);
    tableInfoList.add(ti2);
    tableInfoList.add(ti3);
    tableInfoList.add(ti4);
    tableInfoList.add(ti5);
    tableInfoList.add(ti6);
    tableInfoList.add(ti7);
    return tableInfoList;
  }

  private List<TableInfo> getTableInfoWithIndexTables(
      boolean includeIndexTable, boolean includeIndexedTableIdField) {
    List<TableInfo> tableInfoList = new ArrayList<>();
    TableInfo mainTable =
        TableInfo.newBuilder()
            .setName("main_table")
            .setId(ByteString.copyFromUtf8("000033c0000030008000000000004002"))
            .build();
    TableInfo indexTable =
        TableInfo.newBuilder()
            .setName("main_table_idx")
            .setId(ByteString.copyFromUtf8("000033c0000030008000000000004003"))
            .setRelationType(RelationType.INDEX_TABLE_RELATION)
            .setIndexedTableId(includeIndexedTableIdField ? mainTable.getId().toStringUtf8() : "")
            .build();
    tableInfoList.add(mainTable);
    if (includeIndexTable) {
      tableInfoList.add(indexTable);
    }
    return tableInfoList;
  }
}
