package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.AssertHelper.assertAuditEntry;
import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static com.yugabyte.yw.common.AssertHelper.assertValue;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static org.hamcrest.CoreMatchers.allOf;
import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.when;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.FakeApiHelper;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.CustomerTask.TargetType;
import com.yugabyte.yw.models.CustomerTask.TaskType;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterConfig.XClusterConfigStatusType;
import com.yugabyte.yw.models.XClusterTableConfig;
import java.util.HashSet;
import java.util.List;
import java.util.UUID;
import java.util.stream.Collectors;
import org.junit.Before;
import org.junit.Test;
import play.libs.Json;
import play.mvc.Result;

public class XClusterConfigControllerTest extends FakeDBApplication {

  private Customer customer;
  private Users user;
  private String configName;
  private UUID targetUniverseUUID;
  private HashSet<String> exampleTables;
  private ObjectNode createRequest;
  private UUID taskUUID;
  private String createRequestURI;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    user = ModelFactory.testUser(customer);

    configName = "my-test-config";
    UUID sourceUniverseUUID = UUID.randomUUID();
    createUniverse("my-test-universe-1", sourceUniverseUUID);
    targetUniverseUUID = UUID.randomUUID();
    createUniverse("my-test-universe-2", targetUniverseUUID);

    createRequest =
        Json.newObject()
            .put("name", configName)
            .put("sourceUniverseUUID", sourceUniverseUUID.toString())
            .put("targetUniverseUUID", targetUniverseUUID.toString());

    exampleTables = new HashSet<>();
    exampleTables.add("000030af000030008000000000004000");
    exampleTables.add("000030af000030008000000000004001");

    ArrayNode tables = Json.newArray();
    for (String table : exampleTables) {
      tables.add(table);
    }
    createRequest.putArray("tables").addAll(tables);

    taskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(), any())).thenReturn(taskUUID);

    createRequestURI = "/api/customers/" + customer.uuid + "/xcluster_configs";
  }

  @Test
  public void testCreate() {
    Result result =
        FakeApiHelper.doRequestWithAuthTokenAndBody(
            "POST", createRequestURI, user.createAuthToken(), createRequest);
    assertOk(result);

    List<XClusterConfig> configList = XClusterConfig.getByTargetUniverseUUID(targetUniverseUUID);
    assertEquals(1, configList.size());

    XClusterConfig xClusterConfig = configList.get(0);
    assertEquals(xClusterConfig.name, configName);
    assertEquals(xClusterConfig.status, XClusterConfigStatusType.Init);
    assertEquals(
        xClusterConfig
            .tables
            .stream()
            .map(XClusterTableConfig::getTableID)
            .collect(Collectors.toSet()),
        exampleTables);

    JsonNode resultJson = Json.parse(contentAsString(result));
    assertValue(resultJson, "taskUUID", taskUUID.toString());
    assertValue(resultJson, "resourceUUID", xClusterConfig.uuid.toString());

    CustomerTask customerTask =
        CustomerTask.find.query().where().eq("task_uuid", taskUUID).findOne();
    assertNotNull(customerTask);
    assertThat(customerTask.getCustomerUUID(), allOf(notNullValue(), equalTo(customer.uuid)));
    assertThat(customerTask.getTargetUUID(), allOf(notNullValue(), equalTo(xClusterConfig.uuid)));
    assertThat(customerTask.getTaskUUID(), allOf(notNullValue(), equalTo(taskUUID)));
    assertThat(customerTask.getTarget(), allOf(notNullValue(), equalTo(TargetType.XClusterConfig)));
    assertThat(
        customerTask.getType(), allOf(notNullValue(), equalTo(TaskType.CreateXClusterConfig)));
    assertThat(customerTask.getTargetName(), allOf(notNullValue(), equalTo(configName)));

    assertAuditEntry(1, customer.uuid);
  }

  @Test
  public void testCreateInvalidCustomer() {
    String invalidCustomer = "invalid-customer";
    String uri = "/api/customers/" + invalidCustomer + "/xcluster_configs";

    Result result =
        FakeApiHelper.doRequestWithAuthTokenAndBody(
            "POST", uri, user.createAuthToken(), createRequest);
    assertEquals(contentAsString(result), BAD_REQUEST, result.status());

    List<XClusterConfig> configList = XClusterConfig.getByTargetUniverseUUID(targetUniverseUUID);
    assertEquals(0, configList.size());

    JsonNode resultJson = Json.parse(contentAsString(result));
    assertValue(resultJson, "success", "false");
    assertValue(
        resultJson,
        "error",
        "Cannot parse parameter cUUID as UUID: Invalid UUID string: " + invalidCustomer);

    CustomerTask customerTask =
        CustomerTask.find.query().where().eq("task_uuid", taskUUID).findOne();
    assertNull(customerTask);

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
                    "POST", createRequestURI, user.createAuthToken(), createRequest));
    assertEquals(contentAsString(result), BAD_REQUEST, result.status());

    List<XClusterConfig> configList = XClusterConfig.getByTargetUniverseUUID(targetUniverseUUID);
    assertEquals(0, configList.size());

    JsonNode resultJson = Json.parse(contentAsString(result));
    assertValue(resultJson, "success", "false");
    assertValue(resultJson, "error", "Cannot find universe " + invalidUUID);

    CustomerTask customerTask =
        CustomerTask.find.query().where().eq("task_uuid", taskUUID).findOne();
    assertNull(customerTask);

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
                    "POST", createRequestURI, user.createAuthToken(), createRequest));
    assertEquals(contentAsString(result), BAD_REQUEST, result.status());

    List<XClusterConfig> configList = XClusterConfig.getByTargetUniverseUUID(targetUniverseUUID);
    assertEquals(0, configList.size());

    JsonNode resultJson = Json.parse(contentAsString(result));
    assertValue(resultJson, "success", "false");
    assertValue(resultJson, "error", "Cannot find universe " + invalidUUID);

    CustomerTask customerTask =
        CustomerTask.find.query().where().eq("task_uuid", taskUUID).findOne();
    assertNull(customerTask);

    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testCreateNoName() {
    createRequest.remove("name");

    Result result =
        assertPlatformException(
            () ->
                FakeApiHelper.doRequestWithAuthTokenAndBody(
                    "POST", createRequestURI, user.createAuthToken(), createRequest));
    assertEquals(contentAsString(result), BAD_REQUEST, result.status());

    List<XClusterConfig> configList = XClusterConfig.getByTargetUniverseUUID(targetUniverseUUID);
    assertEquals(0, configList.size());

    JsonNode resultJson = Json.parse(contentAsString(result));
    assertValue(resultJson, "success", "false");
    assertValue(resultJson, "error", "{\"name\":[\"error.required\"]}");

    CustomerTask customerTask =
        CustomerTask.find.query().where().eq("task_uuid", taskUUID).findOne();
    assertNull(customerTask);

    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testCreateNoSourceUniverseUUID() {
    createRequest.remove("sourceUniverseUUID");

    Result result =
        assertPlatformException(
            () ->
                FakeApiHelper.doRequestWithAuthTokenAndBody(
                    "POST", createRequestURI, user.createAuthToken(), createRequest));
    assertEquals(contentAsString(result), BAD_REQUEST, result.status());

    List<XClusterConfig> configList = XClusterConfig.getByTargetUniverseUUID(targetUniverseUUID);
    assertEquals(0, configList.size());

    JsonNode resultJson = Json.parse(contentAsString(result));
    assertValue(resultJson, "success", "false");
    assertValue(resultJson, "error", "{\"sourceUniverseUUID\":[\"error.required\"]}");

    CustomerTask customerTask =
        CustomerTask.find.query().where().eq("task_uuid", taskUUID).findOne();
    assertNull(customerTask);

    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testCreateNoTargetUniverseUUID() {
    createRequest.remove("targetUniverseUUID");

    Result result =
        assertPlatformException(
            () ->
                FakeApiHelper.doRequestWithAuthTokenAndBody(
                    "POST", createRequestURI, user.createAuthToken(), createRequest));
    assertEquals(contentAsString(result), BAD_REQUEST, result.status());

    List<XClusterConfig> configList = XClusterConfig.getByTargetUniverseUUID(targetUniverseUUID);
    assertEquals(0, configList.size());

    JsonNode resultJson = Json.parse(contentAsString(result));
    assertValue(resultJson, "success", "false");
    assertValue(resultJson, "error", "{\"targetUniverseUUID\":[\"error.required\"]}");

    CustomerTask customerTask =
        CustomerTask.find.query().where().eq("task_uuid", taskUUID).findOne();
    assertNull(customerTask);

    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testCreateNoTables() {
    createRequest.remove("tables");

    Result result =
        assertPlatformException(
            () ->
                FakeApiHelper.doRequestWithAuthTokenAndBody(
                    "POST", createRequestURI, user.createAuthToken(), createRequest));
    assertEquals(contentAsString(result), BAD_REQUEST, result.status());

    List<XClusterConfig> configList = XClusterConfig.getByTargetUniverseUUID(targetUniverseUUID);
    assertEquals(0, configList.size());

    JsonNode resultJson = Json.parse(contentAsString(result));
    assertValue(resultJson, "success", "false");
    assertValue(resultJson, "error", "{\"tables\":[\"error.required\"]}");

    CustomerTask customerTask =
        CustomerTask.find.query().where().eq("task_uuid", taskUUID).findOne();
    assertNull(customerTask);

    assertAuditEntry(0, customer.uuid);
  }
}
