package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.AssertHelper.assertAuditEntry;
import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static com.yugabyte.yw.common.AssertHelper.assertUnauthorizedNoException;
import static com.yugabyte.yw.common.AssertHelper.assertValue;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static com.yugabyte.yw.common.ModelFactory.testCustomer;
import static org.hamcrest.CoreMatchers.allOf;
import static org.hamcrest.CoreMatchers.containsString;
import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.anyList;
import static org.mockito.ArgumentMatchers.anyMap;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.doThrow;
import static org.mockito.Mockito.lenient;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableSet;
import com.google.protobuf.ByteString;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.rbac.Permission;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.forms.XClusterConfigCreateFormData;
import com.yugabyte.yw.forms.XClusterConfigEditFormData;
import com.yugabyte.yw.metrics.MetricQueryResponse;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.CustomerTask.TargetType;
import com.yugabyte.yw.models.RuntimeConfigEntry;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterConfig.XClusterConfigStatusType;
import com.yugabyte.yw.models.XClusterTableConfig;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.models.rbac.ResourceGroup;
import com.yugabyte.yw.models.rbac.ResourceGroup.ResourceDefinition;
import com.yugabyte.yw.models.rbac.Role;
import com.yugabyte.yw.models.rbac.Role.RoleType;
import com.yugabyte.yw.models.rbac.RoleBinding;
import com.yugabyte.yw.models.rbac.RoleBinding.RoleBindingType;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Iterator;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import org.apache.commons.lang3.tuple.ImmutablePair;
import org.junit.Before;
import org.junit.Test;
import org.mockito.Mockito;
import org.yb.CommonTypes;
import org.yb.Schema;
import org.yb.cdc.CdcConsumer.ConsumerRegistryPB;
import org.yb.cdc.CdcConsumer.ProducerEntryPB;
import org.yb.cdc.CdcConsumer.StreamEntryPB;
import org.yb.client.GetMasterClusterConfigResponse;
import org.yb.client.GetTableSchemaResponse;
import org.yb.client.ListTablesResponse;
import org.yb.client.YBClient;
import org.yb.master.CatalogEntityInfo;
import org.yb.master.MasterDdlOuterClass;
import org.yb.master.MasterTypes;
import play.libs.Json;
import play.mvc.Result;

public class XClusterConfigControllerTest extends FakeDBApplication {

  private Customer customer;
  private Users user;
  private String configName;
  private String sourceUniverseName;
  private UUID sourceUniverseUUID;
  private Universe sourceUniverse;
  private String targetUniverseName;
  private UUID targetUniverseUUID;
  private Universe targetUniverse;
  private String namespace1Name;
  private String namespace1Id;
  private String exampleTableID1;
  private String exampleStreamID1;
  private String exampleTable1Name;
  private String exampleTableID2;
  private String exampleStreamID2;
  private String exampleTable2Name;
  private Set<String> exampleTables;
  private HashMap<String, String> exampleTablesAndStreamIDs;
  private ObjectNode createRequest;
  private UUID taskUUID;
  private String apiEndpoint;
  private XClusterConfigCreateFormData createFormData;
  private YBClient mockClient;
  private Role role;
  private ResourceDefinition rd1;
  private ResourceDefinition rd2;

  Permission permission1 = new Permission(ResourceType.UNIVERSE, Action.XCLUSTER);

  @Before
  public void setUp() {
    customer = testCustomer("XClusterConfigController-test-customer");
    user = ModelFactory.testUser(customer);
    role =
        Role.create(
            customer.getUuid(),
            "FakeRole1",
            "testDescription",
            RoleType.Custom,
            new HashSet<>(Arrays.asList(permission1)));
    rd1 =
        ResourceDefinition.builder()
            .resourceType(ResourceType.OTHER)
            .resourceUUIDSet(new HashSet<>(Arrays.asList(customer.getUuid())))
            .build();
    rd2 = ResourceDefinition.builder().resourceType(ResourceType.UNIVERSE).allowAll(true).build();

    configName = "XClusterConfigController-test-config";

    sourceUniverseName = "XClusterConfigController-test-universe-1";
    sourceUniverseUUID = UUID.randomUUID();
    sourceUniverse = createUniverse(sourceUniverseName, sourceUniverseUUID);

    targetUniverseName = "XClusterConfigController-test-universe-2";
    targetUniverseUUID = UUID.randomUUID();
    targetUniverse = createUniverse(targetUniverseName, targetUniverseUUID);

    createRequest =
        Json.newObject()
            .put("name", configName)
            .put("sourceUniverseUUID", sourceUniverseUUID.toString())
            .put("targetUniverseUUID", targetUniverseUUID.toString());

    namespace1Name = "ycql-namespace1";
    namespace1Id = UUID.randomUUID().toString();
    exampleTableID1 = "000030af000030008000000000004000";
    exampleStreamID1 = "ec10532900ef42a29a6899c82dd7404f";
    exampleTable1Name = "exampleTable1";
    exampleTableID2 = "000030af000030008000000000004001";
    exampleStreamID2 = "fea203ffca1f48349901e0de2b52c416";
    exampleTable2Name = "exampleTable2";

    exampleTables = new HashSet<>();
    exampleTables.add(exampleTableID1);
    exampleTables.add(exampleTableID2);

    exampleTablesAndStreamIDs = new HashMap<>();
    exampleTablesAndStreamIDs.put(exampleTableID1, exampleStreamID1);
    exampleTablesAndStreamIDs.put(exampleTableID2, exampleStreamID2);

    ArrayNode tables = Json.newArray();
    for (String table : exampleTables) {
      tables.add(table);
    }
    createRequest.putArray("tables").addAll(tables);

    taskUUID = buildTaskInfo(null, TaskType.XClusterConfigSync);
    when(mockCommissioner.submit(any(), any())).thenReturn(taskUUID);

    String targetUniverseMasterAddresses = targetUniverse.getMasterAddresses();
    String targetUniverseCertificate = targetUniverse.getCertificateNodetoNode();
    mockClient = mock(YBClient.class);
    when(mockService.getClient(targetUniverseMasterAddresses, targetUniverseCertificate))
        .thenReturn(mockClient);

    mockTableSchemaResponse(CommonTypes.TableType.YQL_TABLE_TYPE);

    apiEndpoint = "/api/customers/" + customer.getUuid() + "/xcluster_configs";

    createFormData = new XClusterConfigCreateFormData();
    createFormData.name = configName;
    createFormData.sourceUniverseUUID = sourceUniverseUUID;
    createFormData.targetUniverseUUID = targetUniverseUUID;
    createFormData.tables = exampleTables;

    setupMetricValues();
  }

  private void mockTableSchemaResponse(CommonTypes.TableType tableType) {
    GetTableSchemaResponse mockTableSchemaResponseTable1 =
        new GetTableSchemaResponse(
            0,
            "",
            new Schema(Collections.emptyList()),
            namespace1Name,
            "exampleTableID1",
            exampleTableID1,
            null,
            true,
            tableType,
            Collections.emptyList(),
            false);
    GetTableSchemaResponse mockTableSchemaResponseTable2 =
        new GetTableSchemaResponse(
            0,
            "",
            new Schema(Collections.emptyList()),
            namespace1Name,
            "exampleTableID2",
            exampleTableID2,
            null,
            true,
            tableType,
            Collections.emptyList(),
            false);
    try {
      lenient()
          .when(mockClient.getTableSchemaByUUID(exampleTableID1))
          .thenReturn(mockTableSchemaResponseTable1);
      lenient()
          .when(mockClient.getTableSchemaByUUID(exampleTableID2))
          .thenReturn(mockTableSchemaResponseTable2);
    } catch (Exception ignored) {
    }
  }

  private void setupMockClusterConfigWithXCluster(XClusterConfig xClusterConfig) {
    StreamEntryPB.Builder fakeStreamEntry1 =
        StreamEntryPB.newBuilder().setProducerTableId(exampleTableID1);
    StreamEntryPB.Builder fakeStreamEntry2 =
        StreamEntryPB.newBuilder().setProducerTableId(exampleTableID2);

    ProducerEntryPB.Builder fakeProducerEntry =
        ProducerEntryPB.newBuilder()
            .putStreamMap(exampleStreamID1, fakeStreamEntry1.build())
            .putStreamMap(exampleStreamID2, fakeStreamEntry2.build());

    ConsumerRegistryPB.Builder fakeConsumerRegistryBuilder =
        ConsumerRegistryPB.newBuilder()
            .putProducerMap(xClusterConfig.getReplicationGroupName(), fakeProducerEntry.build());

    CatalogEntityInfo.SysClusterConfigEntryPB.Builder fakeClusterConfigBuilder =
        CatalogEntityInfo.SysClusterConfigEntryPB.newBuilder()
            .setConsumerRegistry(fakeConsumerRegistryBuilder.build());

    GetMasterClusterConfigResponse fakeClusterConfigResponse =
        new GetMasterClusterConfigResponse(0, "", fakeClusterConfigBuilder.build(), null);

    try {
      when(mockClient.getMasterClusterConfig()).thenReturn(fakeClusterConfigResponse);
    } catch (Exception e) {
    }
  }

  private void setupMockMetricQueryHelperResponse() {
    // Note: This is completely fake, unlike the real metric response
    JsonNode fakeMetricResponse = Json.newObject().put("value", "0");
    doReturn(fakeMetricResponse)
        .when(mockMetricQueryHelper)
        .query(anyList(), anyMap(), anyMap(), anyBoolean());
  }

  public void setupMetricValues() {
    ArrayList<MetricQueryResponse.Entry> metricValues = new ArrayList<>();
    MetricQueryResponse.Entry entryExampleTableID1 = new MetricQueryResponse.Entry();
    entryExampleTableID1.labels = new HashMap<>();
    entryExampleTableID1.labels.put("table_id", exampleTableID1);
    entryExampleTableID1.values = new ArrayList<>();
    entryExampleTableID1.values.add(ImmutablePair.of(10.0, 0.0));
    metricValues.add(entryExampleTableID1);

    MetricQueryResponse.Entry entryExampleTableID2 = new MetricQueryResponse.Entry();
    entryExampleTableID2.labels = new HashMap<>();
    entryExampleTableID2.labels.put("table_id", exampleTableID2);
    entryExampleTableID2.values = new ArrayList<>();
    entryExampleTableID2.values.add(ImmutablePair.of(10.0, 0.0));
    metricValues.add(entryExampleTableID2);

    doReturn(metricValues).when(mockMetricQueryHelper).queryDirect(any());
  }

  public void initClientGetTablesList() {
    initClientGetTablesList(CommonTypes.TableType.YQL_TABLE_TYPE);
  }

  public void initClientGetTablesList(CommonTypes.TableType tableType) {
    ListTablesResponse mockListTablesResponse = mock(ListTablesResponse.class);
    List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> tableInfoList = new ArrayList<>();
    // Adding table 1.
    MasterDdlOuterClass.ListTablesResponsePB.TableInfo.Builder table1TableInfoBuilder =
        MasterDdlOuterClass.ListTablesResponsePB.TableInfo.newBuilder();
    table1TableInfoBuilder.setTableType(tableType);
    table1TableInfoBuilder.setId(ByteString.copyFromUtf8(exampleTableID1));
    table1TableInfoBuilder.setName(exampleTable1Name);
    table1TableInfoBuilder.setNamespace(
        MasterTypes.NamespaceIdentifierPB.newBuilder()
            .setName(namespace1Name)
            .setId(ByteString.copyFromUtf8(namespace1Id))
            .build());
    tableInfoList.add(table1TableInfoBuilder.build());
    // Adding table 2.
    MasterDdlOuterClass.ListTablesResponsePB.TableInfo.Builder table2TableInfoBuilder =
        MasterDdlOuterClass.ListTablesResponsePB.TableInfo.newBuilder();
    table2TableInfoBuilder.setTableType(tableType);
    table2TableInfoBuilder.setId(ByteString.copyFromUtf8(exampleTableID2));
    table2TableInfoBuilder.setName(exampleTable2Name);
    table2TableInfoBuilder.setNamespace(
        MasterTypes.NamespaceIdentifierPB.newBuilder()
            .setName(namespace1Name)
            .setId(ByteString.copyFromUtf8(namespace1Id))
            .build());
    tableInfoList.add(table2TableInfoBuilder.build());

    try {
      when(mockListTablesResponse.getTableInfoList()).thenReturn(tableInfoList);
      when(mockClient.getTablesList(eq(null), anyBoolean(), eq(null)))
          .thenReturn(mockListTablesResponse);
    } catch (Exception e) {
      e.printStackTrace();
    }
  }

  private void mockDefaultInstanceClusterConfig() {
    GetMasterClusterConfigResponse fakeClusterConfigResponse =
        new GetMasterClusterConfigResponse(
            0, "", CatalogEntityInfo.SysClusterConfigEntryPB.getDefaultInstance(), null);
    try {
      when(mockClient.getMasterClusterConfig()).thenReturn(fakeClusterConfigResponse);
    } catch (Exception ignore) {
    }
  }

  private void validateGetXClusterResponse(
      XClusterConfig expectedXClusterConfig, Result actualResult) {
    JsonNode actual = Json.parse(contentAsString(actualResult));

    XClusterConfig actualXClusterConfig = new XClusterConfig();
    actualXClusterConfig.setUuid(UUID.fromString(actual.get("uuid").asText()));
    actualXClusterConfig.setName(actual.get("name").asText());
    actualXClusterConfig.setSourceUniverseUUID(
        UUID.fromString(actual.get("sourceUniverseUUID").asText()));
    actualXClusterConfig.setTargetUniverseUUID(
        UUID.fromString(actual.get("targetUniverseUUID").asText()));
    actualXClusterConfig.setStatus(XClusterConfigStatusType.valueOf(actual.get("status").asText()));
    actualXClusterConfig.setTables(new HashSet<>());

    Iterator<JsonNode> i = actual.get("tables").elements();
    while (i.hasNext()) {
      XClusterTableConfig tableConfig =
          new XClusterTableConfig(actualXClusterConfig, i.next().asText());
      actualXClusterConfig.getTables().add(tableConfig);
    }

    assertEquals(expectedXClusterConfig.getUuid(), actualXClusterConfig.getUuid());
    assertEquals(expectedXClusterConfig.getName(), actualXClusterConfig.getName());
    assertEquals(
        expectedXClusterConfig.getSourceUniverseUUID(),
        actualXClusterConfig.getSourceUniverseUUID());
    assertEquals(
        expectedXClusterConfig.getTargetUniverseUUID(),
        actualXClusterConfig.getTargetUniverseUUID());
    assertEquals(expectedXClusterConfig.getStatus(), actualXClusterConfig.getStatus());
    assertEquals(expectedXClusterConfig.getTableIds(), actualXClusterConfig.getTableIds());
  }

  private void validateGetXClusterLagResponse(Result result) {
    JsonNode resultJson = Json.parse(contentAsString(result));
    assertValue(resultJson.get("lag"), "value", "0");
  }

  private void assertGetXClusterLagError(String expectedErrStr, Result result) {
    JsonNode resultJson = Json.parse(contentAsString(result));
    assertValue(resultJson.get("lag"), "error", expectedErrStr);
  }

  private void assertNumXClusterConfigs(int expected) {
    List<XClusterConfig> configList = XClusterConfig.getByTargetUniverseUUID(targetUniverseUUID);
    assertEquals(expected, configList.size());
  }

  private void assertResponseError(String expectedErrStr, Result result) {
    JsonNode actual = Json.parse(contentAsString(result));
    assertValue(actual, "success", "false");
    assertValue(actual, "error", expectedErrStr);
  }

  private void assertNoTasksCreated() {
    CustomerTask customerTask =
        CustomerTask.find.query().where().eq("task_uuid", taskUUID).findOne();
    assertNull(customerTask);
  }

  @Test
  public void testCreate() {

    initClientGetTablesList();
    mockDefaultInstanceClusterConfig();
    Result result =
        doRequestWithAuthTokenAndBody("POST", apiEndpoint, user.createAuthToken(), createRequest);
    assertOk(result);

    assertNumXClusterConfigs(1);

    XClusterConfig xClusterConfig =
        XClusterConfig.getByNameSourceTarget(configName, sourceUniverseUUID, targetUniverseUUID);
    assertEquals(xClusterConfig.getName(), configName);
    assertEquals(xClusterConfig.getStatus(), XClusterConfigStatusType.Initialized);
    assertEquals(xClusterConfig.getTableIds(), exampleTables);

    JsonNode resultJson = Json.parse(contentAsString(result));
    assertValue(resultJson, "taskUUID", taskUUID.toString());
    assertValue(resultJson, "resourceUUID", xClusterConfig.getUuid().toString());

    CustomerTask customerTask =
        CustomerTask.find.query().where().eq("task_uuid", taskUUID).findOne();
    assertNotNull(customerTask);
    assertThat(customerTask.getCustomerUUID(), allOf(notNullValue(), equalTo(customer.getUuid())));
    assertThat(
        customerTask.getTargetUUID(),
        allOf(notNullValue(), equalTo(xClusterConfig.getSourceUniverseUUID())));
    assertThat(customerTask.getTaskUUID(), allOf(notNullValue(), equalTo(taskUUID)));
    assertThat(
        customerTask.getTargetType(), allOf(notNullValue(), equalTo(TargetType.XClusterConfig)));
    assertThat(
        customerTask.getType(), allOf(notNullValue(), equalTo(CustomerTask.TaskType.Create)));
    assertThat(customerTask.getTargetName(), allOf(notNullValue(), equalTo(configName)));

    assertAuditEntry(1, customer.getUuid());
  }

  @Test
  public void testCreateUsingNewRbacAuthzWithNeededPermissions() {
    initClientGetTablesList();
    mockDefaultInstanceClusterConfig();
    RuntimeConfigEntry.upsertGlobal("yb.rbac.use_new_authz", "true");
    ResourceGroup rG = new ResourceGroup(new HashSet<>(Arrays.asList(rd1, rd2)));
    RoleBinding.create(user, RoleBindingType.Custom, role, rG);
    Result result =
        doRequestWithAuthTokenAndBody("POST", apiEndpoint, user.createAuthToken(), createRequest);
    assertOk(result);

    assertNumXClusterConfigs(1);

    XClusterConfig xClusterConfig =
        XClusterConfig.getByNameSourceTarget(configName, sourceUniverseUUID, targetUniverseUUID);
    assertEquals(xClusterConfig.getName(), configName);
    assertEquals(xClusterConfig.getStatus(), XClusterConfigStatusType.Initialized);
    assertEquals(xClusterConfig.getTableIds(), exampleTables);

    JsonNode resultJson = Json.parse(contentAsString(result));
    assertValue(resultJson, "taskUUID", taskUUID.toString());
    assertValue(resultJson, "resourceUUID", xClusterConfig.getUuid().toString());

    CustomerTask customerTask =
        CustomerTask.find.query().where().eq("task_uuid", taskUUID).findOne();
    assertNotNull(customerTask);
    assertThat(customerTask.getCustomerUUID(), allOf(notNullValue(), equalTo(customer.getUuid())));
    assertThat(
        customerTask.getTargetUUID(),
        allOf(notNullValue(), equalTo(xClusterConfig.getSourceUniverseUUID())));
    assertThat(customerTask.getTaskUUID(), allOf(notNullValue(), equalTo(taskUUID)));
    assertThat(
        customerTask.getTargetType(), allOf(notNullValue(), equalTo(TargetType.XClusterConfig)));
    assertThat(
        customerTask.getType(), allOf(notNullValue(), equalTo(CustomerTask.TaskType.Create)));
    assertThat(customerTask.getTargetName(), allOf(notNullValue(), equalTo(configName)));

    assertAuditEntry(1, customer.getUuid());
  }

  @Test
  public void testCreateUsingNewRbacAuthzWithNoPermissions() {
    initClientGetTablesList();
    RuntimeConfigEntry.upsertGlobal("yb.rbac.use_new_authz", "true");
    Result result =
        doRequestWithAuthTokenAndBody("POST", apiEndpoint, user.createAuthToken(), createRequest);
    assertUnauthorizedNoException(result, "Unable to authorize user");
  }

  @Test
  public void testCreateUsingNewRbacAuthzWithIncompletePermissionsOnTargetUniverse() {
    initClientGetTablesList();
    RuntimeConfigEntry.upsertGlobal("yb.rbac.use_new_authz", "true");
    ResourceDefinition rd3 =
        ResourceDefinition.builder()
            .resourceType(ResourceType.UNIVERSE)
            .resourceUUIDSet(new HashSet<>(Arrays.asList(sourceUniverseUUID)))
            .build();
    ResourceGroup rG = new ResourceGroup(new HashSet<>(Arrays.asList(rd3)));
    RoleBinding.create(user, RoleBindingType.Custom, role, rG);
    Result result =
        doRequestWithAuthTokenAndBody("POST", apiEndpoint, user.createAuthToken(), createRequest);
    assertUnauthorizedNoException(result, "Unable to authorize user");
  }

  @Test
  public void testCreateUsingNewRbacAuthzWithIncompletePermissionsOnSourceUniverse() {
    initClientGetTablesList();
    RuntimeConfigEntry.upsertGlobal("yb.rbac.use_new_authz", "true");
    ResourceDefinition rd3 =
        ResourceDefinition.builder()
            .resourceType(ResourceType.UNIVERSE)
            .resourceUUIDSet(new HashSet<>(Arrays.asList(targetUniverseUUID)))
            .build();
    ResourceGroup rG = new ResourceGroup(new HashSet<>(Arrays.asList(rd3)));
    RoleBinding.create(user, RoleBindingType.Custom, role, rG);
    Result result =
        doRequestWithAuthTokenAndBody("POST", apiEndpoint, user.createAuthToken(), createRequest);
    assertUnauthorizedNoException(result, "Unable to authorize user");
  }

  @Test
  public void testCreateUsingNewRbacAuthzWithIncompletePermission() {
    initClientGetTablesList();
    RuntimeConfigEntry.upsertGlobal("yb.rbac.use_new_authz", "true");
    Role role1 =
        Role.create(
            customer.getUuid(),
            "FakeRole2",
            "testDescription",
            RoleType.Custom,
            new HashSet<>(Arrays.asList(permission1)));
    ResourceDefinition rd3 =
        ResourceDefinition.builder()
            .resourceType(ResourceType.UNIVERSE)
            .resourceUUIDSet(new HashSet<>(Arrays.asList(targetUniverseUUID)))
            .build();
    ResourceGroup rG = new ResourceGroup(new HashSet<>(Arrays.asList(rd3)));
    RoleBinding.create(user, RoleBindingType.Custom, role1, rG);
    Result result =
        doRequestWithAuthTokenAndBody("POST", apiEndpoint, user.createAuthToken(), createRequest);
    assertUnauthorizedNoException(result, "Unable to authorize user");
  }

  @Test
  public void testCreateInvalidCustomer() {
    String invalidCustomer = "invalid-customer";
    String invalidCustomerAPIEndpoint = "/api/customers/" + invalidCustomer + "/xcluster_configs";

    Result result =
        doRequestWithAuthTokenAndBody(
            "POST", invalidCustomerAPIEndpoint, user.createAuthToken(), createRequest);
    assertEquals(contentAsString(result), BAD_REQUEST, result.status());
    String expectedErrStr =
        String.format(
            "Cannot parse parameter cUUID as UUID: Invalid UUID string: %s", invalidCustomer);
    assertResponseError(expectedErrStr, result);
    assertNumXClusterConfigs(0);
    assertNoTasksCreated();
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testCreateInvalidSourceUniverse() {
    UUID invalidUUID = UUID.randomUUID();
    createRequest.set("sourceUniverseUUID", Json.toJson(invalidUUID));

    Result result =
        assertPlatformException(
            () ->
                doRequestWithAuthTokenAndBody(
                    "POST", apiEndpoint, user.createAuthToken(), createRequest));
    assertEquals(contentAsString(result), BAD_REQUEST, result.status());
    assertResponseError("Cannot find universe " + invalidUUID, result);
    assertNumXClusterConfigs(0);
    assertNoTasksCreated();
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testCreateInvalidTargetUniverse() {
    UUID invalidUUID = UUID.randomUUID();
    createRequest.set("targetUniverseUUID", Json.toJson(invalidUUID));

    Result result =
        assertPlatformException(
            () ->
                doRequestWithAuthTokenAndBody(
                    "POST", apiEndpoint, user.createAuthToken(), createRequest));
    assertEquals(contentAsString(result), BAD_REQUEST, result.status());
    assertResponseError("Cannot find universe " + invalidUUID, result);
    assertNumXClusterConfigs(0);
    assertNoTasksCreated();
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testCreateAlreadyExists() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Running);

    Result result =
        assertPlatformException(
            () ->
                doRequestWithAuthTokenAndBody(
                    "POST", apiEndpoint, user.createAuthToken(), createRequest));
    assertEquals(contentAsString(result), BAD_REQUEST, result.status());
    String expectedErrMsg =
        String.format(
            "xCluster config between source universe %s "
                + "and target universe %s with name '%s' already exists.",
            sourceUniverseUUID, targetUniverseUUID, configName);
    assertResponseError(expectedErrMsg, result);
    assertNumXClusterConfigs(1);
    assertNoTasksCreated();
    assertAuditEntry(0, customer.getUuid());

    xClusterConfig.delete();
  }

  @Test
  public void testCreateNoName() {
    createRequest.remove("name");

    Result result =
        assertPlatformException(
            () ->
                doRequestWithAuthTokenAndBody(
                    "POST", apiEndpoint, user.createAuthToken(), createRequest));
    assertEquals(contentAsString(result), BAD_REQUEST, result.status());
    assertResponseError("{\"name\":[\"error.required\"]}", result);
    assertNumXClusterConfigs(0);
    assertNoTasksCreated();
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testCreateLongName() {
    char[] nameChars = new char[257];
    Arrays.fill(nameChars, 'a');
    String longName = new String(nameChars);
    createRequest.put("name", longName);

    Result result =
        assertPlatformException(
            () ->
                doRequestWithAuthTokenAndBody(
                    "POST", apiEndpoint, user.createAuthToken(), createRequest));
    assertEquals(contentAsString(result), BAD_REQUEST, result.status());
    assertResponseError("{\"name\":[\"error.maxLength\"]}", result);
    assertNumXClusterConfigs(0);
    assertNoTasksCreated();
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testCreateNoSourceUniverseUUID() {
    createRequest.remove("sourceUniverseUUID");

    Result result =
        assertPlatformException(
            () ->
                doRequestWithAuthTokenAndBody(
                    "POST", apiEndpoint, user.createAuthToken(), createRequest));
    assertEquals(contentAsString(result), BAD_REQUEST, result.status());
    assertResponseError("{\"sourceUniverseUUID\":[\"error.required\"]}", result);
    assertNumXClusterConfigs(0);
    assertNoTasksCreated();
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testCreateNoTargetUniverseUUID() {
    createRequest.remove("targetUniverseUUID");

    Result result =
        assertPlatformException(
            () ->
                doRequestWithAuthTokenAndBody(
                    "POST", apiEndpoint, user.createAuthToken(), createRequest));
    assertEquals(contentAsString(result), BAD_REQUEST, result.status());
    assertResponseError("{\"targetUniverseUUID\":[\"error.required\"]}", result);
    assertNumXClusterConfigs(0);
    assertNoTasksCreated();
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testCreateNoTables() {
    createRequest.remove("tables");

    Result result =
        assertPlatformException(
            () ->
                doRequestWithAuthTokenAndBody(
                    "POST", apiEndpoint, user.createAuthToken(), createRequest));
    assertEquals(contentAsString(result), BAD_REQUEST, result.status());
    assertResponseError("{\"tables\":[\"error.required\"]}", result);
    assertNumXClusterConfigs(0);
    assertNoTasksCreated();
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testGet() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Running);

    setupMockClusterConfigWithXCluster(xClusterConfig);
    setupMockMetricQueryHelperResponse();

    String getAPIEndpoint = apiEndpoint + "/" + xClusterConfig.getUuid();

    Result result = doRequestWithAuthToken("GET", getAPIEndpoint, user.createAuthToken());
    assertOk(result);
    validateGetXClusterResponse(xClusterConfig, result);
    validateGetXClusterLagResponse(result);
    assertAuditEntry(0, customer.getUuid());

    xClusterConfig.delete();
  }

  @Test
  public void testGetUsesStreamIDCache() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Running);
    // Set streamIds.
    exampleTables.forEach(
        tableId ->
            xClusterConfig
                .maybeGetTableById(tableId)
                .ifPresent(
                    tableConfig ->
                        tableConfig.setStreamId(exampleTablesAndStreamIDs.get(tableId))));

    setupMockMetricQueryHelperResponse();

    Mockito.doNothing().when(mockXClusterScheduler).syncXClusterConfig(any());

    GetMasterClusterConfigResponse fakeClusterConfigResponse =
        new GetMasterClusterConfigResponse(
            0, "", CatalogEntityInfo.SysClusterConfigEntryPB.getDefaultInstance(), null);
    try {
      when(mockClient.getMasterClusterConfig()).thenReturn(fakeClusterConfigResponse);
    } catch (Exception ignore) {
    }

    String getAPIEndpoint = apiEndpoint + "/" + xClusterConfig.getUuid();

    Result result = doRequestWithAuthToken("GET", getAPIEndpoint, user.createAuthToken());
    assertOk(result);

    // try {
    //   verify(mockClient, times(0)).getMasterClusterConfig();
    // } catch (Exception e) {
    // }

    validateGetXClusterResponse(xClusterConfig, result);
    validateGetXClusterLagResponse(result);
    assertAuditEntry(0, customer.getUuid());

    xClusterConfig.delete();
  }

  @Test
  public void testGetInvalidCustomer() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Running);

    String invalidCustomer = "invalid-customer";
    String invalidCustomerAPIEndpoint = "/api/customers/" + invalidCustomer + "/xcluster_configs";
    String getAPIEndpoint = invalidCustomerAPIEndpoint + "/" + xClusterConfig.getUuid();

    Result result = doRequestWithAuthToken("GET", getAPIEndpoint, user.createAuthToken());
    assertEquals(contentAsString(result), BAD_REQUEST, result.status());
    String expectedErrStr =
        String.format(
            "Cannot parse parameter cUUID as UUID: Invalid UUID string: %s", invalidCustomer);
    assertResponseError(expectedErrStr, result);
    assertNoTasksCreated();
    assertAuditEntry(0, customer.getUuid());

    xClusterConfig.delete();
  }

  @Test
  public void testGetDoesntExist() {
    String nonexistentUUID = UUID.randomUUID().toString();
    String getAPIEndpoint = apiEndpoint + "/" + nonexistentUUID;

    Result result =
        assertPlatformException(
            () -> doRequestWithAuthToken("GET", getAPIEndpoint, user.createAuthToken()));
    assertEquals(contentAsString(result), BAD_REQUEST, result.status());
    String expectedErrMsg = String.format("Cannot find XClusterConfig %s", nonexistentUUID);
    assertResponseError(expectedErrMsg, result);
    assertNoTasksCreated();
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testGetMetricFailure() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Running);
    // Set streamIds.
    exampleTables.forEach(
        tableId ->
            xClusterConfig
                .maybeGetTableById(tableId)
                .ifPresent(
                    tableConfig ->
                        tableConfig.setStreamId(exampleTablesAndStreamIDs.get(tableId))));

    String fakeErrMsg = "failed to fetch metric data";
    doThrow(new PlatformServiceException(INTERNAL_SERVER_ERROR, fakeErrMsg))
        .when(mockMetricQueryHelper)
        .query(any(), any(), any());

    String getAPIEndpoint = apiEndpoint + "/" + xClusterConfig.getUuid();

    Result result = doRequestWithAuthToken("GET", getAPIEndpoint, user.createAuthToken());
    assertOk(result);

    validateGetXClusterResponse(xClusterConfig, result);
    String expectedErrMsg =
        String.format(
            "Failed to get lag metric data for XClusterConfig(%s): %s",
            xClusterConfig.getUuid(), fakeErrMsg);
    assertGetXClusterLagError(expectedErrMsg, result);
    assertAuditEntry(0, customer.getUuid());

    xClusterConfig.delete();
  }

  @Test
  public void testEditName() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Running);

    String editAPIEndpoint = apiEndpoint + "/" + xClusterConfig.getUuid();

    ObjectNode editNameRequest = Json.newObject().put("name", configName + "-renamed");

    Result result =
        doRequestWithAuthTokenAndBody(
            "PUT", editAPIEndpoint, user.createAuthToken(), editNameRequest);
    assertOk(result);

    JsonNode resultJson = Json.parse(contentAsString(result));
    assertValue(resultJson, "taskUUID", taskUUID.toString());

    CustomerTask customerTask =
        CustomerTask.find.query().where().eq("task_uuid", taskUUID).findOne();
    assertNotNull(customerTask);
    assertThat(customerTask.getCustomerUUID(), allOf(notNullValue(), equalTo(customer.getUuid())));
    assertThat(
        customerTask.getTargetUUID(),
        allOf(notNullValue(), equalTo(xClusterConfig.getSourceUniverseUUID())));
    assertThat(customerTask.getTaskUUID(), allOf(notNullValue(), equalTo(taskUUID)));
    assertThat(
        customerTask.getTargetType(), allOf(notNullValue(), equalTo(TargetType.XClusterConfig)));
    assertThat(customerTask.getType(), allOf(notNullValue(), equalTo(CustomerTask.TaskType.Edit)));
    assertThat(customerTask.getTargetName(), allOf(notNullValue(), equalTo(configName)));

    assertAuditEntry(1, customer.getUuid());

    xClusterConfig.delete();
  }

  @Test
  public void testEditTables() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Running);

    initClientGetTablesList();

    String editAPIEndpoint = apiEndpoint + "/" + xClusterConfig.getUuid();

    ArrayNode modifiedTables = Json.newArray();
    modifiedTables.add("000030af000030008000000000004000");
    ObjectNode editTablesRequest = Json.newObject();
    editTablesRequest.putArray("tables").addAll(modifiedTables);

    Result result =
        doRequestWithAuthTokenAndBody(
            "PUT", editAPIEndpoint, user.createAuthToken(), editTablesRequest);
    assertOk(result);

    JsonNode resultJson = Json.parse(contentAsString(result));
    assertValue(resultJson, "taskUUID", taskUUID.toString());

    CustomerTask customerTask =
        CustomerTask.find.query().where().eq("task_uuid", taskUUID).findOne();
    assertNotNull(customerTask);
    assertThat(customerTask.getCustomerUUID(), allOf(notNullValue(), equalTo(customer.getUuid())));
    assertThat(
        customerTask.getTargetUUID(),
        allOf(notNullValue(), equalTo(xClusterConfig.getSourceUniverseUUID())));
    assertThat(customerTask.getTaskUUID(), allOf(notNullValue(), equalTo(taskUUID)));
    assertThat(
        customerTask.getTargetType(), allOf(notNullValue(), equalTo(TargetType.XClusterConfig)));
    assertThat(customerTask.getType(), allOf(notNullValue(), equalTo(CustomerTask.TaskType.Edit)));
    assertThat(customerTask.getTargetName(), allOf(notNullValue(), equalTo(configName)));

    assertAuditEntry(1, customer.getUuid());

    xClusterConfig.delete();
  }

  @Test
  public void testEditStatus() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Running);

    String editAPIEndpoint = apiEndpoint + "/" + xClusterConfig.getUuid();

    ObjectNode editStatusRequest = Json.newObject().put("status", "Paused");

    Result result =
        doRequestWithAuthTokenAndBody(
            "PUT", editAPIEndpoint, user.createAuthToken(), editStatusRequest);
    assertOk(result);

    JsonNode resultJson = Json.parse(contentAsString(result));
    assertValue(resultJson, "taskUUID", taskUUID.toString());

    CustomerTask customerTask =
        CustomerTask.find.query().where().eq("task_uuid", taskUUID).findOne();
    assertNotNull(customerTask);
    assertThat(customerTask.getCustomerUUID(), allOf(notNullValue(), equalTo(customer.getUuid())));
    assertThat(
        customerTask.getTargetUUID(),
        allOf(notNullValue(), equalTo(xClusterConfig.getSourceUniverseUUID())));
    assertThat(customerTask.getTaskUUID(), allOf(notNullValue(), equalTo(taskUUID)));
    assertThat(
        customerTask.getTargetType(), allOf(notNullValue(), equalTo(TargetType.XClusterConfig)));
    assertThat(customerTask.getType(), allOf(notNullValue(), equalTo(CustomerTask.TaskType.Edit)));
    assertThat(customerTask.getTargetName(), allOf(notNullValue(), equalTo(configName)));

    assertAuditEntry(1, customer.getUuid());

    xClusterConfig.delete();
  }

  @Test
  public void testEditInvalidCustomer() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Running);

    String invalidCustomer = "invalid-customer";
    String invalidCustomerAPIEndpoint = "/api/customers/" + invalidCustomer + "/xcluster_configs";
    String editAPIEndpoint = invalidCustomerAPIEndpoint + "/" + xClusterConfig.getUuid();

    ObjectNode editNameRequest = Json.newObject().put("name", configName + "-renamed");

    Result result =
        doRequestWithAuthTokenAndBody(
            "PUT", editAPIEndpoint, user.createAuthToken(), editNameRequest);
    assertEquals(contentAsString(result), BAD_REQUEST, result.status());
    String expectedErrMsg =
        String.format(
            "Cannot parse parameter cUUID as UUID: Invalid UUID string: %s", invalidCustomer);
    assertResponseError(expectedErrMsg, result);
    assertNoTasksCreated();
    assertAuditEntry(0, customer.getUuid());

    xClusterConfig.delete();
  }

  @Test
  public void testEditNoOperation() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Running);

    String editAPIEndpoint = apiEndpoint + "/" + xClusterConfig.getUuid();

    ObjectNode editEmptyRequest = Json.newObject();

    Result result =
        assertPlatformException(
            () ->
                doRequestWithAuthTokenAndBody(
                    "PUT", editAPIEndpoint, user.createAuthToken(), editEmptyRequest));
    assertEquals(contentAsString(result), BAD_REQUEST, result.status());
    assertResponseError("Must specify an edit operation", result);
    assertNoTasksCreated();
    assertAuditEntry(0, customer.getUuid());

    xClusterConfig.delete();
  }

  @Test
  public void testEditMultipleOperations() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Running);

    String editAPIEndpoint = apiEndpoint + "/" + xClusterConfig.getUuid();

    ObjectNode editMultipleOperations = Json.newObject();
    editMultipleOperations.put("name", configName + "-renamed");
    editMultipleOperations.put("status", "Paused");

    Result result =
        assertPlatformException(
            () ->
                doRequestWithAuthTokenAndBody(
                    "PUT", editAPIEndpoint, user.createAuthToken(), editMultipleOperations));
    assertEquals(contentAsString(result), BAD_REQUEST, result.status());
    assertResponseError("Exactly one edit request is allowed in one call", result);
    assertNoTasksCreated();
    assertAuditEntry(0, customer.getUuid());

    xClusterConfig.delete();
  }

  @Test
  public void testEditLongName() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Running);

    String editAPIEndpoint = apiEndpoint + "/" + xClusterConfig.getUuid();

    char[] nameChars = new char[257];
    Arrays.fill(nameChars, 'a');
    String longName = new String(nameChars);
    ObjectNode editStatusRequest = Json.newObject().put("name", longName);

    Result result =
        assertPlatformException(
            () ->
                doRequestWithAuthTokenAndBody(
                    "PUT", editAPIEndpoint, user.createAuthToken(), editStatusRequest));
    assertEquals(contentAsString(result), BAD_REQUEST, result.status());
    assertResponseError("{\"name\":[\"error.maxLength\"]}", result);
    assertNoTasksCreated();
    assertAuditEntry(0, customer.getUuid());

    xClusterConfig.delete();
  }

  @Test
  public void testEditInvalidStatus() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Running);

    String editAPIEndpoint = apiEndpoint + "/" + xClusterConfig.getUuid();

    ObjectNode editStatusRequest = Json.newObject().put("status", "foobar");

    Result result =
        assertPlatformException(
            () ->
                doRequestWithAuthTokenAndBody(
                    "PUT", editAPIEndpoint, user.createAuthToken(), editStatusRequest));
    assertEquals(contentAsString(result), BAD_REQUEST, result.status());
    assertResponseError(
        "{\"status\":[\"status can be set either to `Running` or `Paused`\"]}", result);
    assertNoTasksCreated();
    assertAuditEntry(0, customer.getUuid());

    xClusterConfig.delete();
  }

  @Test
  public void testEditNoTables() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Running);

    String editAPIEndpoint = apiEndpoint + "/" + xClusterConfig.getUuid();

    ObjectNode editTablesRequest = Json.newObject();
    editTablesRequest.putArray("tables");

    Result result =
        assertPlatformException(
            () ->
                doRequestWithAuthTokenAndBody(
                    "PUT", editAPIEndpoint, user.createAuthToken(), editTablesRequest));
    assertEquals(contentAsString(result), BAD_REQUEST, result.status());
    assertResponseError("Must specify an edit operation", result);
    assertNoTasksCreated();
    assertAuditEntry(0, customer.getUuid());

    xClusterConfig.delete();
  }

  @Test
  public void testEditDoesntExist() {
    String nonexistentUUID = UUID.randomUUID().toString();
    String editAPIEndpoint = apiEndpoint + "/" + nonexistentUUID;

    ObjectNode editNameRequest = Json.newObject().put("name", configName + "-renamed");

    Result result =
        assertPlatformException(
            () ->
                doRequestWithAuthTokenAndBody(
                    "PUT", editAPIEndpoint, user.createAuthToken(), editNameRequest));
    assertEquals(contentAsString(result), BAD_REQUEST, result.status());
    String expectedErrMsg = String.format("Cannot find XClusterConfig %s", nonexistentUUID);
    assertResponseError(expectedErrMsg, result);
    assertNoTasksCreated();
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testEditAlreadyExists() {
    XClusterConfigCreateFormData previousXClusterCreateFormData =
        new XClusterConfigCreateFormData();
    previousXClusterCreateFormData.name = configName + "-new";
    previousXClusterCreateFormData.sourceUniverseUUID = sourceUniverseUUID;
    previousXClusterCreateFormData.targetUniverseUUID = targetUniverseUUID;
    previousXClusterCreateFormData.tables = exampleTables;
    XClusterConfig previousXClusterConfig =
        XClusterConfig.create(previousXClusterCreateFormData, XClusterConfigStatusType.Running);

    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Running);

    String editAPIEndpoint = apiEndpoint + "/" + xClusterConfig.getUuid();

    ObjectNode editNameRequest = Json.newObject().put("name", configName + "-new");

    Result result =
        assertPlatformException(
            () ->
                doRequestWithAuthTokenAndBody(
                    "PUT", editAPIEndpoint, user.createAuthToken(), editNameRequest));
    assertEquals(contentAsString(result), BAD_REQUEST, result.status());
    assertResponseError(
        String.format(
            "xCluster config between source universe %s and target universe %s with name '%s' "
                + "already exists.",
            sourceUniverseUUID, targetUniverseUUID, configName + "-new"),
        result);
    assertNoTasksCreated();
    assertAuditEntry(0, customer.getUuid());

    xClusterConfig.delete();
    previousXClusterConfig.delete();
  }

  @Test
  public void testDelete() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Running);

    String deleteAPIEndpoint = apiEndpoint + "/" + xClusterConfig.getUuid();

    Result result = doRequestWithAuthToken("DELETE", deleteAPIEndpoint, user.createAuthToken());
    assertOk(result);

    JsonNode resultJson = Json.parse(contentAsString(result));
    assertValue(resultJson, "taskUUID", taskUUID.toString());

    CustomerTask customerTask =
        CustomerTask.find.query().where().eq("task_uuid", taskUUID).findOne();
    assertNotNull(customerTask);
    assertThat(customerTask.getCustomerUUID(), allOf(notNullValue(), equalTo(customer.getUuid())));
    assertThat(
        customerTask.getTargetUUID(),
        allOf(notNullValue(), equalTo(xClusterConfig.getSourceUniverseUUID())));
    assertThat(customerTask.getTaskUUID(), allOf(notNullValue(), equalTo(taskUUID)));
    assertThat(
        customerTask.getTargetType(), allOf(notNullValue(), equalTo(TargetType.XClusterConfig)));
    assertThat(
        customerTask.getType(), allOf(notNullValue(), equalTo(CustomerTask.TaskType.Delete)));
    assertThat(customerTask.getTargetName(), allOf(notNullValue(), equalTo(configName)));

    assertAuditEntry(1, customer.getUuid());

    xClusterConfig.delete();
  }

  @Test
  public void testDeleteInvalidCustomer() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Running);

    String invalidCustomer = "invalid-customer";
    String invalidCustomerAPIEndpoint = "/api/customers/" + invalidCustomer + "/xcluster_configs";
    String deleteAPIEndpoint = invalidCustomerAPIEndpoint + "/" + xClusterConfig.getUuid();

    Result result = doRequestWithAuthToken("DELETE", deleteAPIEndpoint, user.createAuthToken());
    assertEquals(contentAsString(result), BAD_REQUEST, result.status());
    String expectedErrMsg =
        String.format(
            "Cannot parse parameter cUUID as UUID: Invalid UUID string: %s", invalidCustomer);
    assertResponseError(expectedErrMsg, result);
    assertNoTasksCreated();
    assertAuditEntry(0, customer.getUuid());

    xClusterConfig.delete();
  }

  @Test
  public void testDeleteDoesntExist() {
    String nonexistentUUID = UUID.randomUUID().toString();
    String deleteAPIEndpoint = apiEndpoint + "/" + nonexistentUUID;

    Result result =
        assertPlatformException(
            () -> doRequestWithAuthToken("DELETE", deleteAPIEndpoint, user.createAuthToken()));
    assertEquals(contentAsString(result), BAD_REQUEST, result.status());
    String expectedErrMsg = String.format("Cannot find XClusterConfig %s", nonexistentUUID);
    assertResponseError(expectedErrMsg, result);
    assertNoTasksCreated();
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testSync() {
    String syncAPIEndpoint = apiEndpoint + "/sync?targetUniverseUUID=" + targetUniverseUUID;

    Result result = doRequestWithAuthToken("POST", syncAPIEndpoint, user.createAuthToken());
    assertOk(result);

    JsonNode resultJson = Json.parse(contentAsString(result));
    assertValue(resultJson, "taskUUID", taskUUID.toString());

    CustomerTask customerTask =
        CustomerTask.find.query().where().eq("task_uuid", taskUUID).findOne();
    assertNotNull(customerTask);
    assertThat(customerTask.getCustomerUUID(), allOf(notNullValue(), equalTo(customer.getUuid())));
    assertThat(customerTask.getTargetUUID(), allOf(notNullValue(), equalTo(targetUniverseUUID)));
    assertThat(customerTask.getTaskUUID(), allOf(notNullValue(), equalTo(taskUUID)));
    assertThat(
        customerTask.getTargetType(), allOf(notNullValue(), equalTo(TargetType.XClusterConfig)));
    assertThat(customerTask.getType(), allOf(notNullValue(), equalTo(CustomerTask.TaskType.Sync)));
    assertThat(customerTask.getTargetName(), allOf(notNullValue(), equalTo(targetUniverseName)));

    assertAuditEntry(1, customer.getUuid());
  }

  @Test
  public void testSyncInvalidCustomer() {
    String invalidCustomer = "invalid-customer";
    String invalidCustomerAPIEndpoint = "/api/customers/" + invalidCustomer + "/xcluster_configs";
    String syncAPIEndpoint =
        invalidCustomerAPIEndpoint + "/sync?targetUniverseUUID=" + targetUniverseUUID;

    Result result = doRequestWithAuthToken("POST", syncAPIEndpoint, user.createAuthToken());
    assertEquals(contentAsString(result), BAD_REQUEST, result.status());
    String expectedErrMsg =
        String.format(
            "Cannot parse parameter cUUID as UUID: Invalid UUID string: %s", invalidCustomer);
    assertResponseError(expectedErrMsg, result);
    assertNoTasksCreated();
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testSyncInvalidTargetUniverse() {
    String invalidUUID = UUID.randomUUID().toString();
    String syncAPIEndpoint = apiEndpoint + "/sync?targetUniverseUUID=" + invalidUUID;

    Result result =
        assertPlatformException(
            () -> doRequestWithAuthToken("POST", syncAPIEndpoint, user.createAuthToken()));
    assertEquals(contentAsString(result), BAD_REQUEST, result.status());
    assertResponseError("Cannot find universe " + invalidUUID, result);
    assertNoTasksCreated();
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testSyncInvalidTargetUniverseUUID() {
    String invalidUUID = "foo";
    String syncAPIEndpoint = apiEndpoint + "/sync?targetUniverseUUID=" + invalidUUID;

    Result result = doRequestWithAuthToken("POST", syncAPIEndpoint, user.createAuthToken());
    assertEquals(contentAsString(result), BAD_REQUEST, result.status());
    String expectedErrMsg =
        String.format(
            "Cannot parse parameter targetUniverseUUID as UUID: " + "Invalid UUID string: %s",
            invalidUUID);
    assertResponseError(expectedErrMsg, result);
    assertNoTasksCreated();
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testSyncWithReplicationGroupName() {
    ObjectNode syncRequestBody = Json.newObject();
    syncRequestBody.put("replicationGroupName", configName);
    syncRequestBody.put("targetUniverseUUID", targetUniverseUUID.toString());

    String syncAPIEndpoint = apiEndpoint + "/sync";
    Result result =
        doRequestWithAuthTokenAndBody(
            "POST", syncAPIEndpoint, user.createAuthToken(), syncRequestBody);

    assertOk(result);
    CustomerTask customerTask =
        CustomerTask.find.query().where().eq("task_uuid", taskUUID).findOne();
    assertNotNull(customerTask);
    assertThat(customerTask.getCustomerUUID(), allOf(notNullValue(), equalTo(customer.getUuid())));
    assertThat(customerTask.getTargetUUID(), allOf(notNullValue(), equalTo(targetUniverseUUID)));
    assertThat(customerTask.getTaskUUID(), allOf(notNullValue(), equalTo(taskUUID)));
    assertThat(
        customerTask.getTargetType(), allOf(notNullValue(), equalTo(TargetType.XClusterConfig)));
    assertThat(customerTask.getType(), allOf(notNullValue(), equalTo(CustomerTask.TaskType.Sync)));
    assertThat(customerTask.getTargetName(), allOf(notNullValue(), equalTo(targetUniverseName)));

    assertAuditEntry(1, customer.getUuid());
  }

  @Test
  public void testSyncWithReplGroupNameInvalidTargetUniverseUUID() {
    String invalidUUID = UUID.randomUUID().toString();
    ObjectNode syncRequestBody = Json.newObject();
    syncRequestBody.put("replicationGroupName", configName);
    syncRequestBody.put("targetUniverseUUID", invalidUUID);

    String syncAPIEndpoint = apiEndpoint + "/sync";
    Result result =
        assertPlatformException(
            () ->
                doRequestWithAuthTokenAndBody(
                    "POST", syncAPIEndpoint, user.createAuthToken(), syncRequestBody));
    assertEquals(contentAsString(result), BAD_REQUEST, result.status());
    assertResponseError("Cannot find universe " + invalidUUID, result);
    assertNoTasksCreated();
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testCreateXClusterConfigWithBootstrapRequiredOnPartialTables() {

    mockTableSchemaResponse(CommonTypes.TableType.PGSQL_TABLE_TYPE);
    initClientGetTablesList(CommonTypes.TableType.PGSQL_TABLE_TYPE);

    createFormData.tables = ImmutableSet.of(exampleTableID1);
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Running);

    XClusterConfigCreateFormData.BootstrapParams params =
        new XClusterConfigCreateFormData.BootstrapParams();
    params.allowBootstrap = false;
    params.tables = Collections.singleton(exampleTableID2);
    params.backupRequestParams =
        new XClusterConfigCreateFormData.BootstrapParams.BootstrapBackupParams();
    params.backupRequestParams.storageConfigUUID =
        ModelFactory.createS3StorageConfig(customer, "s3-config").getConfigUUID();
    XClusterConfigEditFormData editFormData = new XClusterConfigEditFormData();
    editFormData.tables = ImmutableSet.of(exampleTableID1, exampleTableID2);
    editFormData.bootstrapParams = params;

    GetMasterClusterConfigResponse fakeClusterConfigResponse =
        new GetMasterClusterConfigResponse(
            0, "", CatalogEntityInfo.SysClusterConfigEntryPB.getDefaultInstance(), null);
    try {
      when(mockClient.getMasterClusterConfig()).thenReturn(fakeClusterConfigResponse);
    } catch (Exception ignore) {
    }

    String editAPIEndpoint = apiEndpoint + "/" + xClusterConfig.getUuid();
    IllegalArgumentException exception =
        assertThrows(
            IllegalArgumentException.class,
            () ->
                doRequestWithAuthTokenAndBody(
                    "PUT", editAPIEndpoint, user.createAuthToken(), Json.toJson(editFormData)));
    assertThat(
        exception.getMessage(),
        containsString("For YSQL tables, all the tables in a keyspace must be selected"));

    params.allowBootstrap = true;
    Result result =
        doRequestWithAuthTokenAndBody(
            "PUT", editAPIEndpoint, user.createAuthToken(), Json.toJson(editFormData));
    assertOk(result);

    JsonNode resultJson = Json.parse(contentAsString(result));
    assertValue(resultJson, "taskUUID", taskUUID.toString());

    CustomerTask customerTask =
        CustomerTask.find.query().where().eq("task_uuid", taskUUID).findOne();
    assertNotNull(customerTask);
    assertThat(customerTask.getCustomerUUID(), allOf(notNullValue(), equalTo(customer.getUuid())));
    assertThat(
        customerTask.getTargetUUID(),
        allOf(notNullValue(), equalTo(xClusterConfig.getSourceUniverseUUID())));
    assertThat(customerTask.getTaskUUID(), allOf(notNullValue(), equalTo(taskUUID)));
    assertThat(
        customerTask.getTargetType(), allOf(notNullValue(), equalTo(TargetType.XClusterConfig)));
    assertThat(customerTask.getType(), allOf(notNullValue(), equalTo(CustomerTask.TaskType.Edit)));
    assertThat(customerTask.getTargetName(), allOf(notNullValue(), equalTo(configName)));

    assertAuditEntry(1, customer.getUuid());

    xClusterConfig.delete();
  }
}
