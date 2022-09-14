package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.AssertHelper.assertAuditEntry;
import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static com.yugabyte.yw.common.AssertHelper.assertValue;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static com.yugabyte.yw.common.ModelFactory.testCustomer;
import static org.hamcrest.CoreMatchers.allOf;
import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.anyList;
import static org.mockito.ArgumentMatchers.anyMap;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.doThrow;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.FakeApiHelper;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.XClusterConfigCreateFormData;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.CustomerTask.TargetType;
import com.yugabyte.yw.models.CustomerTask.TaskType;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterConfig.XClusterConfigStatusType;
import com.yugabyte.yw.models.XClusterTableConfig;
import java.util.Arrays;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Iterator;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.yb.cdc.CdcConsumer.ConsumerRegistryPB;
import org.yb.cdc.CdcConsumer.ProducerEntryPB;
import org.yb.cdc.CdcConsumer.StreamEntryPB;
import org.yb.client.GetMasterClusterConfigResponse;
import org.yb.client.YBClient;
import org.yb.master.CatalogEntityInfo;
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
  private String exampleTableID1;
  private String exampleStreamID1;
  private String exampleTableID2;
  private String exampleStreamID2;
  private Set<String> exampleTables;
  private HashMap<String, String> exampleTablesAndStreamIDs;
  private ObjectNode createRequest;
  private UUID taskUUID;
  private String apiEndpoint;
  private XClusterConfigCreateFormData createFormData;
  private YBClient mockClient;

  @Before
  public void setUp() {
    customer = testCustomer("XClusterConfigController-test-customer");
    user = ModelFactory.testUser(customer);

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

    exampleTableID1 = "000030af000030008000000000004000";
    exampleStreamID1 = "ec10532900ef42a29a6899c82dd7404f";
    exampleTableID2 = "000030af000030008000000000004001";
    exampleStreamID2 = "fea203ffca1f48349901e0de2b52c416";

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

    taskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(), any())).thenReturn(taskUUID);

    String targetUniverseMasterAddresses = targetUniverse.getMasterAddresses();
    String targetUniverseCertificate = targetUniverse.getCertificateNodetoNode();
    mockClient = mock(YBClient.class);
    when(mockService.getClient(targetUniverseMasterAddresses, targetUniverseCertificate))
        .thenReturn(mockClient);

    apiEndpoint = "/api/customers/" + customer.uuid + "/xcluster_configs";

    createFormData = new XClusterConfigCreateFormData();
    createFormData.name = configName;
    createFormData.sourceUniverseUUID = sourceUniverseUUID;
    createFormData.targetUniverseUUID = targetUniverseUUID;
    createFormData.tables = exampleTables;
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

  private void validateGetXClusterResponse(
      XClusterConfig expectedXClusterConfig, Result actualResult) {
    JsonNode actual = Json.parse(contentAsString(actualResult));

    XClusterConfig actualXClusterConfig = new XClusterConfig();
    actualXClusterConfig.uuid = UUID.fromString(actual.get("uuid").asText());
    actualXClusterConfig.name = actual.get("name").asText();
    actualXClusterConfig.sourceUniverseUUID =
        UUID.fromString(actual.get("sourceUniverseUUID").asText());
    actualXClusterConfig.targetUniverseUUID =
        UUID.fromString(actual.get("targetUniverseUUID").asText());
    actualXClusterConfig.status = XClusterConfigStatusType.valueOf(actual.get("status").asText());
    actualXClusterConfig.tables = new HashSet<>();

    Iterator<JsonNode> i = actual.get("tables").elements();
    while (i.hasNext()) {
      XClusterTableConfig tableConfig =
          new XClusterTableConfig(actualXClusterConfig, i.next().asText());
      actualXClusterConfig.tables.add(tableConfig);
    }

    assertEquals(expectedXClusterConfig.uuid, actualXClusterConfig.uuid);
    assertEquals(expectedXClusterConfig.name, actualXClusterConfig.name);
    assertEquals(
        expectedXClusterConfig.sourceUniverseUUID, actualXClusterConfig.sourceUniverseUUID);
    assertEquals(
        expectedXClusterConfig.targetUniverseUUID, actualXClusterConfig.targetUniverseUUID);
    assertEquals(expectedXClusterConfig.status, actualXClusterConfig.status);
    assertEquals(expectedXClusterConfig.getTables(), actualXClusterConfig.getTables());
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
    Result result =
        FakeApiHelper.doRequestWithAuthTokenAndBody(
            "POST", apiEndpoint, user.createAuthToken(), createRequest);
    assertOk(result);

    assertNumXClusterConfigs(1);

    XClusterConfig xClusterConfig =
        XClusterConfig.getByNameSourceTarget(configName, sourceUniverseUUID, targetUniverseUUID);
    assertEquals(xClusterConfig.name, configName);
    assertEquals(xClusterConfig.status, XClusterConfigStatusType.Initialized);
    assertEquals(xClusterConfig.getTables(), exampleTables);

    JsonNode resultJson = Json.parse(contentAsString(result));
    assertValue(resultJson, "taskUUID", taskUUID.toString());
    assertValue(resultJson, "resourceUUID", xClusterConfig.uuid.toString());

    CustomerTask customerTask =
        CustomerTask.find.query().where().eq("task_uuid", taskUUID).findOne();
    assertNotNull(customerTask);
    assertThat(customerTask.getCustomerUUID(), allOf(notNullValue(), equalTo(customer.uuid)));
    assertThat(
        customerTask.getTargetUUID(),
        allOf(notNullValue(), equalTo(xClusterConfig.targetUniverseUUID)));
    assertThat(customerTask.getTaskUUID(), allOf(notNullValue(), equalTo(taskUUID)));
    assertThat(customerTask.getTarget(), allOf(notNullValue(), equalTo(TargetType.XClusterConfig)));
    assertThat(customerTask.getType(), allOf(notNullValue(), equalTo(TaskType.Create)));
    assertThat(customerTask.getTargetName(), allOf(notNullValue(), equalTo(configName)));

    assertAuditEntry(1, customer.uuid);
  }

  @Test
  public void testCreateInvalidCustomer() {
    String invalidCustomer = "invalid-customer";
    String invalidCustomerAPIEndpoint = "/api/customers/" + invalidCustomer + "/xcluster_configs";

    Result result =
        FakeApiHelper.doRequestWithAuthTokenAndBody(
            "POST", invalidCustomerAPIEndpoint, user.createAuthToken(), createRequest);
    assertEquals(contentAsString(result), BAD_REQUEST, result.status());
    String expectedErrStr =
        String.format(
            "Cannot parse parameter cUUID as UUID: Invalid UUID string: %s", invalidCustomer);
    assertResponseError(expectedErrStr, result);
    assertNumXClusterConfigs(0);
    assertNoTasksCreated();
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testCreateInvalidSourceUniverse() {
    UUID invalidUUID = UUID.randomUUID();
    createRequest.set("sourceUniverseUUID", Json.toJson(invalidUUID));

    Result result =
        assertPlatformException(
            () ->
                FakeApiHelper.doRequestWithAuthTokenAndBody(
                    "POST", apiEndpoint, user.createAuthToken(), createRequest));
    assertEquals(contentAsString(result), BAD_REQUEST, result.status());
    assertResponseError("Cannot find universe " + invalidUUID, result);
    assertNumXClusterConfigs(0);
    assertNoTasksCreated();
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testCreateInvalidTargetUniverse() {
    UUID invalidUUID = UUID.randomUUID();
    createRequest.set("targetUniverseUUID", Json.toJson(invalidUUID));

    Result result =
        assertPlatformException(
            () ->
                FakeApiHelper.doRequestWithAuthTokenAndBody(
                    "POST", apiEndpoint, user.createAuthToken(), createRequest));
    assertEquals(contentAsString(result), BAD_REQUEST, result.status());
    assertResponseError("Cannot find universe " + invalidUUID, result);
    assertNumXClusterConfigs(0);
    assertNoTasksCreated();
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testCreateAlreadyExists() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Running);

    Result result =
        assertPlatformException(
            () ->
                FakeApiHelper.doRequestWithAuthTokenAndBody(
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
    assertAuditEntry(0, customer.uuid);

    xClusterConfig.delete();
  }

  @Test
  public void testCreateNoName() {
    createRequest.remove("name");

    Result result =
        assertPlatformException(
            () ->
                FakeApiHelper.doRequestWithAuthTokenAndBody(
                    "POST", apiEndpoint, user.createAuthToken(), createRequest));
    assertEquals(contentAsString(result), BAD_REQUEST, result.status());
    assertResponseError("{\"name\":[\"error.required\"]}", result);
    assertNumXClusterConfigs(0);
    assertNoTasksCreated();
    assertAuditEntry(0, customer.uuid);
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
                FakeApiHelper.doRequestWithAuthTokenAndBody(
                    "POST", apiEndpoint, user.createAuthToken(), createRequest));
    assertEquals(contentAsString(result), BAD_REQUEST, result.status());
    assertResponseError("{\"name\":[\"error.maxLength\"]}", result);
    assertNumXClusterConfigs(0);
    assertNoTasksCreated();
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testCreateNoSourceUniverseUUID() {
    createRequest.remove("sourceUniverseUUID");

    Result result =
        assertPlatformException(
            () ->
                FakeApiHelper.doRequestWithAuthTokenAndBody(
                    "POST", apiEndpoint, user.createAuthToken(), createRequest));
    assertEquals(contentAsString(result), BAD_REQUEST, result.status());
    assertResponseError("{\"sourceUniverseUUID\":[\"error.required\"]}", result);
    assertNumXClusterConfigs(0);
    assertNoTasksCreated();
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testCreateNoTargetUniverseUUID() {
    createRequest.remove("targetUniverseUUID");

    Result result =
        assertPlatformException(
            () ->
                FakeApiHelper.doRequestWithAuthTokenAndBody(
                    "POST", apiEndpoint, user.createAuthToken(), createRequest));
    assertEquals(contentAsString(result), BAD_REQUEST, result.status());
    assertResponseError("{\"targetUniverseUUID\":[\"error.required\"]}", result);
    assertNumXClusterConfigs(0);
    assertNoTasksCreated();
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testCreateNoTables() {
    createRequest.remove("tables");

    Result result =
        assertPlatformException(
            () ->
                FakeApiHelper.doRequestWithAuthTokenAndBody(
                    "POST", apiEndpoint, user.createAuthToken(), createRequest));
    assertEquals(contentAsString(result), BAD_REQUEST, result.status());
    assertResponseError("{\"tables\":[\"error.required\"]}", result);
    assertNumXClusterConfigs(0);
    assertNoTasksCreated();
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testGet() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Running);

    setupMockClusterConfigWithXCluster(xClusterConfig);
    setupMockMetricQueryHelperResponse();

    String getAPIEndpoint = apiEndpoint + "/" + xClusterConfig.uuid;

    Result result =
        FakeApiHelper.doRequestWithAuthToken("GET", getAPIEndpoint, user.createAuthToken());
    assertOk(result);
    validateGetXClusterResponse(xClusterConfig, result);
    validateGetXClusterLagResponse(result);
    assertAuditEntry(0, customer.uuid);

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
                    tableConfig -> tableConfig.streamId = exampleTablesAndStreamIDs.get(tableId)));

    setupMockMetricQueryHelperResponse();

    String getAPIEndpoint = apiEndpoint + "/" + xClusterConfig.uuid;

    Result result =
        FakeApiHelper.doRequestWithAuthToken("GET", getAPIEndpoint, user.createAuthToken());
    assertOk(result);

    try {
      verify(mockClient, times(0)).getMasterClusterConfig();
    } catch (Exception e) {
    }

    validateGetXClusterResponse(xClusterConfig, result);
    validateGetXClusterLagResponse(result);
    assertAuditEntry(0, customer.uuid);

    xClusterConfig.delete();
  }

  @Test
  public void testGetInvalidCustomer() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Running);

    String invalidCustomer = "invalid-customer";
    String invalidCustomerAPIEndpoint = "/api/customers/" + invalidCustomer + "/xcluster_configs";
    String getAPIEndpoint = invalidCustomerAPIEndpoint + "/" + xClusterConfig.uuid;

    Result result =
        FakeApiHelper.doRequestWithAuthToken("GET", getAPIEndpoint, user.createAuthToken());
    assertEquals(contentAsString(result), BAD_REQUEST, result.status());
    String expectedErrStr =
        String.format(
            "Cannot parse parameter cUUID as UUID: Invalid UUID string: %s", invalidCustomer);
    assertResponseError(expectedErrStr, result);
    assertNoTasksCreated();
    assertAuditEntry(0, customer.uuid);

    xClusterConfig.delete();
  }

  @Test
  public void testGetDoesntExist() {
    String nonexistentUUID = UUID.randomUUID().toString();
    String getAPIEndpoint = apiEndpoint + "/" + nonexistentUUID;

    Result result =
        assertPlatformException(
            () ->
                FakeApiHelper.doRequestWithAuthToken(
                    "GET", getAPIEndpoint, user.createAuthToken()));
    assertEquals(contentAsString(result), BAD_REQUEST, result.status());
    String expectedErrMsg = String.format("Cannot find XClusterConfig %s", nonexistentUUID);
    assertResponseError(expectedErrMsg, result);
    assertNoTasksCreated();
    assertAuditEntry(0, customer.uuid);
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
                    tableConfig -> tableConfig.streamId = exampleTablesAndStreamIDs.get(tableId)));

    String fakeErrMsg = "failed to fetch metric data";
    doThrow(new PlatformServiceException(INTERNAL_SERVER_ERROR, fakeErrMsg))
        .when(mockMetricQueryHelper)
        .query(any(), any(), any());

    String getAPIEndpoint = apiEndpoint + "/" + xClusterConfig.uuid;

    Result result =
        FakeApiHelper.doRequestWithAuthToken("GET", getAPIEndpoint, user.createAuthToken());
    assertOk(result);

    validateGetXClusterResponse(xClusterConfig, result);
    String expectedErrMsg =
        String.format(
            "Failed to get lag metric data for XClusterConfig(%s): %s",
            xClusterConfig.uuid, fakeErrMsg);
    assertGetXClusterLagError(expectedErrMsg, result);
    assertAuditEntry(0, customer.uuid);

    xClusterConfig.delete();
  }

  @Test
  public void testEditName() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Running);

    String editAPIEndpoint = apiEndpoint + "/" + xClusterConfig.uuid;

    ObjectNode editNameRequest = Json.newObject().put("name", configName + "-renamed");

    Result result =
        FakeApiHelper.doRequestWithAuthTokenAndBody(
            "PUT", editAPIEndpoint, user.createAuthToken(), editNameRequest);
    assertOk(result);

    JsonNode resultJson = Json.parse(contentAsString(result));
    assertValue(resultJson, "taskUUID", taskUUID.toString());

    CustomerTask customerTask =
        CustomerTask.find.query().where().eq("task_uuid", taskUUID).findOne();
    assertNotNull(customerTask);
    assertThat(customerTask.getCustomerUUID(), allOf(notNullValue(), equalTo(customer.uuid)));
    assertThat(
        customerTask.getTargetUUID(),
        allOf(notNullValue(), equalTo(xClusterConfig.targetUniverseUUID)));
    assertThat(customerTask.getTaskUUID(), allOf(notNullValue(), equalTo(taskUUID)));
    assertThat(customerTask.getTarget(), allOf(notNullValue(), equalTo(TargetType.XClusterConfig)));
    assertThat(customerTask.getType(), allOf(notNullValue(), equalTo(TaskType.Edit)));
    assertThat(customerTask.getTargetName(), allOf(notNullValue(), equalTo(configName)));

    assertAuditEntry(1, customer.uuid);

    xClusterConfig.delete();
  }

  @Test
  public void testEditTables() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Running);

    String editAPIEndpoint = apiEndpoint + "/" + xClusterConfig.uuid;

    ArrayNode modifiedTables = Json.newArray();
    modifiedTables.add("000030af000030008000000000004000");
    ObjectNode editTablesRequest = Json.newObject();
    editTablesRequest.putArray("tables").addAll(modifiedTables);

    Result result =
        FakeApiHelper.doRequestWithAuthTokenAndBody(
            "PUT", editAPIEndpoint, user.createAuthToken(), editTablesRequest);
    assertOk(result);

    JsonNode resultJson = Json.parse(contentAsString(result));
    assertValue(resultJson, "taskUUID", taskUUID.toString());

    CustomerTask customerTask =
        CustomerTask.find.query().where().eq("task_uuid", taskUUID).findOne();
    assertNotNull(customerTask);
    assertThat(customerTask.getCustomerUUID(), allOf(notNullValue(), equalTo(customer.uuid)));
    assertThat(
        customerTask.getTargetUUID(),
        allOf(notNullValue(), equalTo(xClusterConfig.targetUniverseUUID)));
    assertThat(customerTask.getTaskUUID(), allOf(notNullValue(), equalTo(taskUUID)));
    assertThat(customerTask.getTarget(), allOf(notNullValue(), equalTo(TargetType.XClusterConfig)));
    assertThat(customerTask.getType(), allOf(notNullValue(), equalTo(TaskType.Edit)));
    assertThat(customerTask.getTargetName(), allOf(notNullValue(), equalTo(configName)));

    assertAuditEntry(1, customer.uuid);

    xClusterConfig.delete();
  }

  @Test
  public void testEditStatus() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Running);

    String editAPIEndpoint = apiEndpoint + "/" + xClusterConfig.uuid;

    ObjectNode editStatusRequest = Json.newObject().put("status", "Paused");

    Result result =
        FakeApiHelper.doRequestWithAuthTokenAndBody(
            "PUT", editAPIEndpoint, user.createAuthToken(), editStatusRequest);
    assertOk(result);

    JsonNode resultJson = Json.parse(contentAsString(result));
    assertValue(resultJson, "taskUUID", taskUUID.toString());

    CustomerTask customerTask =
        CustomerTask.find.query().where().eq("task_uuid", taskUUID).findOne();
    assertNotNull(customerTask);
    assertThat(customerTask.getCustomerUUID(), allOf(notNullValue(), equalTo(customer.uuid)));
    assertThat(
        customerTask.getTargetUUID(),
        allOf(notNullValue(), equalTo(xClusterConfig.targetUniverseUUID)));
    assertThat(customerTask.getTaskUUID(), allOf(notNullValue(), equalTo(taskUUID)));
    assertThat(customerTask.getTarget(), allOf(notNullValue(), equalTo(TargetType.XClusterConfig)));
    assertThat(customerTask.getType(), allOf(notNullValue(), equalTo(TaskType.Edit)));
    assertThat(customerTask.getTargetName(), allOf(notNullValue(), equalTo(configName)));

    assertAuditEntry(1, customer.uuid);

    xClusterConfig.delete();
  }

  @Test
  public void testEditInvalidCustomer() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Running);

    String invalidCustomer = "invalid-customer";
    String invalidCustomerAPIEndpoint = "/api/customers/" + invalidCustomer + "/xcluster_configs";
    String editAPIEndpoint = invalidCustomerAPIEndpoint + "/" + xClusterConfig.uuid;

    ObjectNode editNameRequest = Json.newObject().put("name", configName + "-renamed");

    Result result =
        FakeApiHelper.doRequestWithAuthTokenAndBody(
            "PUT", editAPIEndpoint, user.createAuthToken(), editNameRequest);
    assertEquals(contentAsString(result), BAD_REQUEST, result.status());
    String expectedErrMsg =
        String.format(
            "Cannot parse parameter cUUID as UUID: Invalid UUID string: %s", invalidCustomer);
    assertResponseError(expectedErrMsg, result);
    assertNoTasksCreated();
    assertAuditEntry(0, customer.uuid);

    xClusterConfig.delete();
  }

  @Test
  public void testEditNoOperation() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Running);

    String editAPIEndpoint = apiEndpoint + "/" + xClusterConfig.uuid;

    ObjectNode editEmptyRequest = Json.newObject();

    Result result =
        assertPlatformException(
            () ->
                FakeApiHelper.doRequestWithAuthTokenAndBody(
                    "PUT", editAPIEndpoint, user.createAuthToken(), editEmptyRequest));
    assertEquals(contentAsString(result), BAD_REQUEST, result.status());
    assertResponseError("Must specify an edit operation", result);
    assertNoTasksCreated();
    assertAuditEntry(0, customer.uuid);

    xClusterConfig.delete();
  }

  @Test
  public void testEditMultipleOperations() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Running);

    String editAPIEndpoint = apiEndpoint + "/" + xClusterConfig.uuid;

    ObjectNode editMultipleOperations = Json.newObject();
    editMultipleOperations.put("name", configName + "-renamed");
    editMultipleOperations.put("status", "Paused");

    Result result =
        assertPlatformException(
            () ->
                FakeApiHelper.doRequestWithAuthTokenAndBody(
                    "PUT", editAPIEndpoint, user.createAuthToken(), editMultipleOperations));
    assertEquals(contentAsString(result), BAD_REQUEST, result.status());
    assertResponseError(
        "Exactly one edit request (either editName, editStatus, "
            + "editTables) is allowed in one call.",
        result);
    assertNoTasksCreated();
    assertAuditEntry(0, customer.uuid);

    xClusterConfig.delete();
  }

  @Test
  public void testEditLongName() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Running);

    String editAPIEndpoint = apiEndpoint + "/" + xClusterConfig.uuid;

    char[] nameChars = new char[257];
    Arrays.fill(nameChars, 'a');
    String longName = new String(nameChars);
    ObjectNode editStatusRequest = Json.newObject().put("name", longName);

    Result result =
        assertPlatformException(
            () ->
                FakeApiHelper.doRequestWithAuthTokenAndBody(
                    "PUT", editAPIEndpoint, user.createAuthToken(), editStatusRequest));
    assertEquals(contentAsString(result), BAD_REQUEST, result.status());
    assertResponseError("{\"name\":[\"error.maxLength\"]}", result);
    assertNoTasksCreated();
    assertAuditEntry(0, customer.uuid);

    xClusterConfig.delete();
  }

  @Test
  public void testEditInvalidStatus() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Running);

    String editAPIEndpoint = apiEndpoint + "/" + xClusterConfig.uuid;

    ObjectNode editStatusRequest = Json.newObject().put("status", "foobar");

    Result result =
        assertPlatformException(
            () ->
                FakeApiHelper.doRequestWithAuthTokenAndBody(
                    "PUT", editAPIEndpoint, user.createAuthToken(), editStatusRequest));
    assertEquals(contentAsString(result), BAD_REQUEST, result.status());
    assertResponseError("{\"status\":[\"error.pattern\"]}", result);
    assertNoTasksCreated();
    assertAuditEntry(0, customer.uuid);

    xClusterConfig.delete();
  }

  @Test
  public void testEditNoTables() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Running);

    String editAPIEndpoint = apiEndpoint + "/" + xClusterConfig.uuid;

    ObjectNode editTablesRequest = Json.newObject();
    editTablesRequest.putArray("tables");

    Result result =
        assertPlatformException(
            () ->
                FakeApiHelper.doRequestWithAuthTokenAndBody(
                    "PUT", editAPIEndpoint, user.createAuthToken(), editTablesRequest));
    assertEquals(contentAsString(result), BAD_REQUEST, result.status());
    assertResponseError("Must specify an edit operation", result);
    assertNoTasksCreated();
    assertAuditEntry(0, customer.uuid);

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
                FakeApiHelper.doRequestWithAuthTokenAndBody(
                    "PUT", editAPIEndpoint, user.createAuthToken(), editNameRequest));
    assertEquals(contentAsString(result), BAD_REQUEST, result.status());
    String expectedErrMsg = String.format("Cannot find XClusterConfig %s", nonexistentUUID);
    assertResponseError(expectedErrMsg, result);
    assertNoTasksCreated();
    assertAuditEntry(0, customer.uuid);
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

    String editAPIEndpoint = apiEndpoint + "/" + xClusterConfig.uuid;

    ObjectNode editNameRequest = Json.newObject().put("name", configName + "-new");

    Result result =
        assertPlatformException(
            () ->
                FakeApiHelper.doRequestWithAuthTokenAndBody(
                    "PUT", editAPIEndpoint, user.createAuthToken(), editNameRequest));
    assertEquals(contentAsString(result), BAD_REQUEST, result.status());
    assertResponseError("XClusterConfig with same name already exists", result);
    assertNoTasksCreated();
    assertAuditEntry(0, customer.uuid);

    xClusterConfig.delete();
    previousXClusterConfig.delete();
  }

  @Test
  public void testDelete() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Running);

    String deleteAPIEndpoint = apiEndpoint + "/" + xClusterConfig.uuid;

    Result result =
        FakeApiHelper.doRequestWithAuthToken("DELETE", deleteAPIEndpoint, user.createAuthToken());
    assertOk(result);

    JsonNode resultJson = Json.parse(contentAsString(result));
    assertValue(resultJson, "taskUUID", taskUUID.toString());

    CustomerTask customerTask =
        CustomerTask.find.query().where().eq("task_uuid", taskUUID).findOne();
    assertNotNull(customerTask);
    assertThat(customerTask.getCustomerUUID(), allOf(notNullValue(), equalTo(customer.uuid)));
    assertThat(
        customerTask.getTargetUUID(),
        allOf(notNullValue(), equalTo(xClusterConfig.targetUniverseUUID)));
    assertThat(customerTask.getTaskUUID(), allOf(notNullValue(), equalTo(taskUUID)));
    assertThat(customerTask.getTarget(), allOf(notNullValue(), equalTo(TargetType.XClusterConfig)));
    assertThat(customerTask.getType(), allOf(notNullValue(), equalTo(TaskType.Delete)));
    assertThat(customerTask.getTargetName(), allOf(notNullValue(), equalTo(configName)));

    assertAuditEntry(1, customer.uuid);

    xClusterConfig.delete();
  }

  @Test
  public void testDeleteInvalidCustomer() {
    XClusterConfig xClusterConfig =
        XClusterConfig.create(createFormData, XClusterConfigStatusType.Running);

    String invalidCustomer = "invalid-customer";
    String invalidCustomerAPIEndpoint = "/api/customers/" + invalidCustomer + "/xcluster_configs";
    String deleteAPIEndpoint = invalidCustomerAPIEndpoint + "/" + xClusterConfig.uuid;

    Result result =
        FakeApiHelper.doRequestWithAuthToken("DELETE", deleteAPIEndpoint, user.createAuthToken());
    assertEquals(contentAsString(result), BAD_REQUEST, result.status());
    String expectedErrMsg =
        String.format(
            "Cannot parse parameter cUUID as UUID: Invalid UUID string: %s", invalidCustomer);
    assertResponseError(expectedErrMsg, result);
    assertNoTasksCreated();
    assertAuditEntry(0, customer.uuid);

    xClusterConfig.delete();
  }

  @Test
  public void testDeleteDoesntExist() {
    String nonexistentUUID = UUID.randomUUID().toString();
    String deleteAPIEndpoint = apiEndpoint + "/" + nonexistentUUID;

    Result result =
        assertPlatformException(
            () ->
                FakeApiHelper.doRequestWithAuthToken(
                    "DELETE", deleteAPIEndpoint, user.createAuthToken()));
    assertEquals(contentAsString(result), BAD_REQUEST, result.status());
    String expectedErrMsg = String.format("Cannot find XClusterConfig %s", nonexistentUUID);
    assertResponseError(expectedErrMsg, result);
    assertNoTasksCreated();
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testSync() {
    String syncAPIEndpoint = apiEndpoint + "/sync?targetUniverseUUID=" + targetUniverseUUID;

    Result result =
        FakeApiHelper.doRequestWithAuthToken("POST", syncAPIEndpoint, user.createAuthToken());
    assertOk(result);

    JsonNode resultJson = Json.parse(contentAsString(result));
    assertValue(resultJson, "taskUUID", taskUUID.toString());

    CustomerTask customerTask =
        CustomerTask.find.query().where().eq("task_uuid", taskUUID).findOne();
    assertNotNull(customerTask);
    assertThat(customerTask.getCustomerUUID(), allOf(notNullValue(), equalTo(customer.uuid)));
    assertThat(customerTask.getTargetUUID(), allOf(notNullValue(), equalTo(targetUniverseUUID)));
    assertThat(customerTask.getTaskUUID(), allOf(notNullValue(), equalTo(taskUUID)));
    assertThat(customerTask.getTarget(), allOf(notNullValue(), equalTo(TargetType.XClusterConfig)));
    assertThat(customerTask.getType(), allOf(notNullValue(), equalTo(TaskType.Sync)));
    assertThat(customerTask.getTargetName(), allOf(notNullValue(), equalTo(targetUniverseName)));

    assertAuditEntry(1, customer.uuid);
  }

  @Test
  public void testSyncInvalidCustomer() {
    String invalidCustomer = "invalid-customer";
    String invalidCustomerAPIEndpoint = "/api/customers/" + invalidCustomer + "/xcluster_configs";
    String syncAPIEndpoint =
        invalidCustomerAPIEndpoint + "/sync?targetUniverseUUID=" + targetUniverseUUID;

    Result result =
        FakeApiHelper.doRequestWithAuthToken("POST", syncAPIEndpoint, user.createAuthToken());
    assertEquals(contentAsString(result), BAD_REQUEST, result.status());
    String expectedErrMsg =
        String.format(
            "Cannot parse parameter cUUID as UUID: Invalid UUID string: %s", invalidCustomer);
    assertResponseError(expectedErrMsg, result);
    assertNoTasksCreated();
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testSyncInvalidTargetUniverse() {
    String invalidUUID = UUID.randomUUID().toString();
    String syncAPIEndpoint = apiEndpoint + "/sync?targetUniverseUUID=" + invalidUUID;

    Result result =
        assertPlatformException(
            () ->
                FakeApiHelper.doRequestWithAuthToken(
                    "POST", syncAPIEndpoint, user.createAuthToken()));
    assertEquals(contentAsString(result), BAD_REQUEST, result.status());
    assertResponseError("Cannot find universe " + invalidUUID, result);
    assertNoTasksCreated();
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testSyncInvalidTargetUniverseUUID() {
    String invalidUUID = "foo";
    String syncAPIEndpoint = apiEndpoint + "/sync?targetUniverseUUID=" + invalidUUID;

    Result result =
        FakeApiHelper.doRequestWithAuthToken("POST", syncAPIEndpoint, user.createAuthToken());
    assertEquals(contentAsString(result), BAD_REQUEST, result.status());
    String expectedErrMsg =
        String.format(
            "Cannot parse parameter targetUniverseUUID as UUID: " + "Invalid UUID string: %s",
            invalidUUID);
    assertResponseError(expectedErrMsg, result);
    assertNoTasksCreated();
    assertAuditEntry(0, customer.uuid);
  }
}
