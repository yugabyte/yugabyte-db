// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.AssertHelper.assertAuditEntry;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static com.yugabyte.yw.common.AssertHelper.assertValue;
import static com.yugabyte.yw.common.AssertHelper.assertValues;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static com.yugabyte.yw.models.CustomerTask.TaskType.Create;
import static com.yugabyte.yw.models.CustomerTask.TaskType.GFlagsUpgrade;
import static com.yugabyte.yw.models.CustomerTask.TaskType.Update;
import static com.yugabyte.yw.models.CustomerTask.TaskType.UpgradeSoftware;
import static org.hamcrest.CoreMatchers.allOf;
import static org.hamcrest.CoreMatchers.containsString;
import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.is;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThat;
import static org.junit.Assert.assertTrue;
import static org.mockito.Mockito.when;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.FORBIDDEN;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.contentAsString;
import static play.test.Helpers.fakeRequest;
import static play.test.Helpers.route;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.common.FakeApiHelper;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.config.impl.RuntimeConfig;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.helpers.TaskType;
import io.ebean.Model;
import java.util.Calendar;
import java.util.Optional;
import java.util.UUID;
import java.util.concurrent.TimeUnit;
import java.util.stream.IntStream;
import org.hamcrest.MatcherAssert;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;
import play.libs.Json;
import play.mvc.Result;

@RunWith(MockitoJUnitRunner.class)
public class CustomerTaskControllerTest extends FakeDBApplication {
  private Customer customer;
  private Users user;
  private Universe universe;

  @Mock private RuntimeConfig<Model> config;

  @Mock RuntimeConfigFactory mockRuntimeConfigFactory;

  @InjectMocks private CustomerTaskController controller;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    user = ModelFactory.testUser(customer);
    universe = createUniverse(customer.getCustomerId());
    when(mockRuntimeConfigFactory.globalRuntimeConf()).thenReturn(config);
  }

  @Test
  public void testTaskHistoryEmptyList() {
    String authToken = user.createAuthToken();
    Result result =
        route(
            fakeRequest("GET", "/api/customers/" + customer.uuid + "/tasks")
                .header("X-AUTH-TOKEN", authToken));

    assertEquals(OK, result.status());

    JsonNode json = Json.parse(contentAsString(result));
    assertTrue(json.isObject());
    assertEquals(0, json.size());
    assertAuditEntry(0, customer.uuid);
  }

  private UUID createTaskWithStatus(
      UUID targetUUID,
      CustomerTask.TargetType targetType,
      CustomerTask.TaskType taskType,
      TaskType taskInfoType,
      String targetName,
      String status,
      double percentComplete) {
    ObjectNode responseJson = Json.newObject();
    UUID taskUUID =
        createTaskWithStatusAndResponse(
            targetUUID,
            targetType,
            taskType,
            taskInfoType,
            targetName,
            status,
            percentComplete,
            responseJson);
    when(mockCommissioner.mayGetStatus(taskUUID)).thenReturn(Optional.of(responseJson));
    return taskUUID;
  }

  private UUID createTaskWithStatusAndResponse(
      UUID targetUUID,
      CustomerTask.TargetType targetType,
      CustomerTask.TaskType taskType,
      TaskType taskInfoType,
      String targetName,
      String status,
      double percentComplete,
      ObjectNode responseJson) {
    UUID taskUUID = UUID.randomUUID();
    TaskInfo taskInfo = new TaskInfo(taskInfoType);
    taskInfo.setTaskUUID(taskUUID);
    taskInfo.setTaskDetails(Json.newObject());
    taskInfo.setOwner("");
    taskInfo.save();
    CustomerTask task =
        CustomerTask.create(customer, targetUUID, taskUUID, targetType, taskType, targetName);
    responseJson.put("status", status);
    responseJson.put("percent", percentComplete);
    responseJson.put("title", task.getFriendlyDescription());
    responseJson.put("createTime", task.getCreateTime().toString());
    responseJson.put("target", targetName);
    responseJson.put("targetUUID", targetUUID.toString());
    responseJson.put("type", taskType.name());
    responseJson.put("typeName", taskType.getFriendlyName());
    if (percentComplete == 100.0) {
      // Sleep 3 seconds so that the completed time is greater than
      // creation time.
      try {
        TimeUnit.SECONDS.sleep(3);
        task.markAsCompleted();
      } catch (Exception e) {
        // Do nothing
      }
    }
    return taskUUID;
  }

  private UUID createSubTask(
      UUID parentUUID, int position, TaskType taskType, TaskInfo.State taskState) {
    return createSubTaskWithResponse(parentUUID, position, taskType, taskState, null);
  }

  private UUID createSubTaskWithResponse(
      UUID parentUUID,
      int position,
      TaskType taskType,
      TaskInfo.State taskState,
      ObjectNode responseJson) {
    // Persist subtask
    UserTaskDetails.SubTaskGroupType groupType = UserTaskDetails.SubTaskGroupType.ConfigureUniverse;
    TaskInfo subTask = new TaskInfo(taskType);
    subTask.setParentUuid(parentUUID);
    subTask.setPosition(position);
    subTask.setSubTaskGroupType(groupType);
    subTask.setTaskState(taskState);
    ObjectNode taskDetailsJson = Json.newObject();
    taskDetailsJson.put("errorString", taskState.equals(TaskInfo.State.Failure) ? "foobaz" : null);
    subTask.setTaskDetails(taskDetailsJson);
    subTask.setOwner("foobar");
    subTask.save();

    // Add info to responseJson
    if (responseJson != null) {
      JsonNode detailsJson = responseJson.get("details");
      UserTaskDetails details =
          (detailsJson == null)
              ? new UserTaskDetails()
              : Json.fromJson(detailsJson, UserTaskDetails.class);
      UserTaskDetails.SubTaskDetails subTaskDetails = UserTaskDetails.createSubTask(groupType);
      subTaskDetails.setState(taskState);
      details.add(subTaskDetails);
      responseJson.set("details", Json.toJson(details));
    }

    return subTask.getTaskUUID();
  }

  @Test
  public void testUpgradeSoftwareTask() {
    String authToken = user.createAuthToken();
    UUID universeUUID = UUID.randomUUID();
    ObjectNode versionNumbers = Json.newObject();
    final String YB_SOFTWARE_VERSION = "ybSoftwareVersion";
    final String YB_PREV_SOFTWARE_VERSION = "ybPrevSoftwareVersion";
    versionNumbers.put(YB_SOFTWARE_VERSION, "{Previous Version}");
    versionNumbers.put(YB_PREV_SOFTWARE_VERSION, "{Current Version}");
    UUID upgradeUUID =
        createTaskWithStatus(
            universeUUID,
            CustomerTask.TargetType.Universe,
            UpgradeSoftware,
            TaskType.SoftwareUpgrade,
            "Foo",
            "Success",
            100.0);
    String url = "/api/customers/" + customer.uuid + "/tasks";
    Result result = FakeApiHelper.doRequestWithAuthToken("GET", url, authToken);
    //    assertEquals(OK, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(OK, result.status());
    assertTrue(json.isObject());
    JsonNode universeTasks = json.get(universeUUID.toString());
    JsonNode upgradeTask = universeTasks.get(0);

    TaskInfo taskInfo = TaskInfo.get(upgradeUUID);
    taskInfo.setTaskDetails(versionNumbers);
    JsonNode taskDetails = taskInfo.getTaskDetails();
    assertTrue(
        ((upgradeTask.get("type").asText().equals("UpgradeSoftware")
                && taskDetails.has(YB_PREV_SOFTWARE_VERSION)))
            || (!upgradeTask.get("type").asText().equals("UpgradeSoftware")
                && !taskDetails.has(YB_SOFTWARE_VERSION)));
  }

  @Test
  public void testFetchTaskWithFailedSubtasks() {
    String authToken = user.createAuthToken();
    UUID universeUUID = UUID.randomUUID();
    UUID taskUUID =
        createTaskWithStatus(
            universeUUID,
            CustomerTask.TargetType.Universe,
            Create,
            TaskType.CreateUniverse,
            "Foo",
            "Failure",
            50.0);
    UUID subTaskUUID =
        createSubTask(taskUUID, 0, TaskType.AnsibleSetupServer, TaskInfo.State.Failure);

    String url = "/api/customers/" + customer.uuid + "/tasks/" + taskUUID + "/failed";
    Result result = FakeApiHelper.doRequestWithAuthToken("GET", url, authToken);
    assertEquals(OK, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertTrue(json.isObject());
    JsonNode failedSubTasks = json.get("failedSubTasks");
    assertTrue(failedSubTasks.isArray());
    JsonNode task = failedSubTasks.get(0);
    assertThat(
        task.get("subTaskUUID").asText(), allOf(notNullValue(), equalTo(subTaskUUID.toString())));
    assertThat(
        task.get("subTaskType").asText(),
        allOf(notNullValue(), equalTo(TaskType.AnsibleSetupServer.name())));
    assertThat(
        task.get("subTaskState").asText(),
        allOf(notNullValue(), equalTo(TaskInfo.State.Failure.toString())));
    assertThat(
        task.get("subTaskGroupType").asText(),
        allOf(notNullValue(), equalTo(UserTaskDetails.SubTaskGroupType.ConfigureUniverse.name())));
    assertThat(task.get("creationTime").asText(), is(notNullValue()));
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testTaskHistoryList() {
    String authToken = user.createAuthToken();
    UUID universeUUID = UUID.randomUUID();
    UUID taskUUID =
        createTaskWithStatus(
            universeUUID,
            CustomerTask.TargetType.Universe,
            Create,
            TaskType.CreateUniverse,
            "Foo",
            "Running",
            50.0);

    UUID providerUUID = UUID.randomUUID();
    UUID providerTaskUUID1 =
        createTaskWithStatus(
            providerUUID,
            CustomerTask.TargetType.Provider,
            Create,
            TaskType.CreateUniverse,
            "Foo",
            "Success",
            100.0);
    UUID providerTaskUUID2 =
        createTaskWithStatus(
            providerUUID,
            CustomerTask.TargetType.Provider,
            Update,
            TaskType.UpgradeUniverse,
            "Foo",
            "Running",
            10.0);

    String url = "/api/customers/" + customer.uuid + "/tasks";
    Result result = FakeApiHelper.doRequestWithAuthToken("GET", url, authToken);
    assertEquals(OK, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertTrue(json.isObject());
    assertEquals(2, json.size());
    JsonNode universeTasks = json.get(universeUUID.toString());
    assertTrue(universeTasks.isArray());
    assertEquals(1, universeTasks.size());
    assertValues(universeTasks, "id", ImmutableList.of(taskUUID.toString()));
    JsonNode task = universeTasks.get(0);
    assertThat(
        task.get("title").asText(), allOf(notNullValue(), equalTo("Creating Universe : Foo")));
    assertThat(task.get("percentComplete").asDouble(), allOf(notNullValue(), equalTo(50.0)));
    assertThat(task.get("status").asText(), allOf(notNullValue(), equalTo("Running")));
    assertTrue(task.get("createTime").asLong() < Calendar.getInstance().getTimeInMillis());
    assertTrue(!task.has("completionTime"));
    assertThat(task.get("target").asText(), allOf(notNullValue(), equalTo("Universe")));
    assertThat(
        task.get("targetUUID").asText(), allOf(notNullValue(), equalTo(universeUUID.toString())));
    JsonNode providerTasks = json.get(providerUUID.toString());
    assertTrue(providerTasks.isArray());
    assertEquals(2, providerTasks.size());
    assertValues(
        providerTasks,
        "id",
        ImmutableList.of(providerTaskUUID1.toString(), providerTaskUUID2.toString()));
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testTasksListHistory() {
    String authToken = user.createAuthToken();

    UUID providerUUID = UUID.randomUUID();
    createTaskWithStatus(
        providerUUID,
        CustomerTask.TargetType.Provider,
        Update,
        TaskType.UpgradeUniverse,
        "Foo",
        "Running",
        10.0);

    String url = "/api/customers/" + customer.uuid + "/tasks_list";
    Result result = FakeApiHelper.doRequestWithAuthToken("GET", url, authToken);
    assertEquals(OK, result.status());
    JsonNode universeTasks = Json.parse(contentAsString(result));

    assertTrue(universeTasks.isArray());
    assertEquals(1, universeTasks.size());
    JsonNode task = universeTasks.get(0);
    assertThat(
        task.get("title").asText(), allOf(notNullValue(), equalTo("Updating Provider : Foo")));
    assertThat(task.get("percentComplete").asDouble(), allOf(notNullValue(), equalTo(10.0)));
    assertThat(task.get("status").asText(), allOf(notNullValue(), equalTo("Running")));
    assertTrue(task.get("createTime").asLong() < Calendar.getInstance().getTimeInMillis());
    assertTrue(!task.has("completionTime"));
    assertThat(task.get("target").asText(), allOf(notNullValue(), equalTo("Provider")));
    assertThat(
        task.get("targetUUID").asText(), allOf(notNullValue(), equalTo(providerUUID.toString())));
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testTaskListWithUniverseUUID() {
    String authToken = user.createAuthToken();
    Universe universe1 = createUniverse("Universe 2", customer.getCustomerId());

    UUID taskUUID1 =
        createTaskWithStatus(
            universe.universeUUID,
            CustomerTask.TargetType.Universe,
            Create,
            TaskType.CreateUniverse,
            "Foo",
            "Running",
            50.0);
    createTaskWithStatus(
        universe1.universeUUID,
        CustomerTask.TargetType.Universe,
        Create,
        TaskType.CreateUniverse,
        "Bar",
        "Running",
        90.0);
    String url = "/api/customers/" + customer.uuid + "/tasks_list?uUUID=" + universe.universeUUID;
    Result result = FakeApiHelper.doRequestWithAuthToken("GET", url, authToken);
    assertEquals(OK, result.status());
    JsonNode universeTasks = Json.parse(contentAsString(result));

    assertTrue(universeTasks.isArray());
    assertEquals(1, universeTasks.size());
    assertValues(universeTasks, "id", ImmutableList.of(taskUUID1.toString()));
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testFriendlyNames() {
    String authToken = user.createAuthToken();
    UUID taskUUID =
        createTaskWithStatus(
            universe.universeUUID,
            CustomerTask.TargetType.Universe,
            GFlagsUpgrade,
            TaskType.GFlagsUpgrade,
            "Foo",
            "Success",
            100.0);
    Result result =
        FakeApiHelper.doRequestWithAuthToken(
            "GET", "/api/customers/" + customer.uuid + "/tasks", authToken);
    CustomerTask ct =
        CustomerTask.find.query().where().eq("task_uuid", taskUUID.toString()).findOne();
    assertEquals(OK, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    JsonNode universeTasks = json.get(universe.universeUUID.toString());
    assertTrue(universeTasks.isArray());
    JsonNode task = universeTasks.get(0);
    MatcherAssert.assertThat(task.get("typeName").asText(), equalTo("GFlags Upgrade"));
  }

  @Test
  public void testTaskCompletionTime() {
    String authToken = user.createAuthToken();
    UUID taskUUID =
        createTaskWithStatus(
            universe.universeUUID,
            CustomerTask.TargetType.Universe,
            Create,
            TaskType.CreateUniverse,
            "Foo",
            "Success",
            100.0);

    String markedCompletionTime = null;
    for (int idx = 0; idx < 2; idx++) {
      String url = "/api/customers/" + customer.uuid + "/tasks";
      Result result = FakeApiHelper.doRequestWithAuthToken("GET", url, authToken);
      assertEquals(OK, result.status());
      assertAuditEntry(0, customer.uuid);
      JsonNode tasksJson = Json.parse(contentAsString(result));
      JsonNode universeTasks = tasksJson.get(universe.universeUUID.toString());
      if (idx == 0) {
        markedCompletionTime = universeTasks.get(0).get("completionTime").asText();
        try {
          Thread.sleep(1000);
        } catch (InterruptedException e) {
          e.printStackTrace();
        }
      } else {
        assertEquals(universeTasks.get(0).get("completionTime").asText(), markedCompletionTime);
      }
    }
  }

  @Test
  public void testTaskHistoryUniverseList() {
    String authToken = user.createAuthToken();
    Universe universe1 = createUniverse("Universe 2", customer.getCustomerId());

    UUID taskUUID1 =
        createTaskWithStatus(
            universe.universeUUID,
            CustomerTask.TargetType.Universe,
            Create,
            TaskType.CreateUniverse,
            "Foo",
            "Running",
            50.0);
    createTaskWithStatus(
        universe1.universeUUID,
        CustomerTask.TargetType.Universe,
        Create,
        TaskType.CreateUniverse,
        "Bar",
        "Running",
        90.0);
    Result result =
        FakeApiHelper.doRequestWithAuthToken(
            "GET",
            "/api/customers/" + customer.uuid + "/universes/" + universe.universeUUID + "/tasks",
            authToken);
    assertEquals(OK, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertTrue(json.isObject());
    JsonNode universeTasks = json.get(universe.universeUUID.toString());
    assertTrue(universeTasks.isArray());
    assertEquals(1, universeTasks.size());
    assertValues(universeTasks, "id", ImmutableList.of(taskUUID1.toString()));
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testTaskHistoryLimit() {
    String authToken = user.createAuthToken();
    Universe universe1 = createUniverse("Universe 2", customer.getCustomerId());
    when(config.getInt(CustomerTaskController.CUSTOMER_TASK_DB_QUERY_LIMIT)).thenReturn(25);
    IntStream.range(0, 100)
        .forEach(
            i ->
                createTaskWithStatus(
                    universe.universeUUID,
                    CustomerTask.TargetType.Universe,
                    Create,
                    TaskType.CreateUniverse,
                    "Foo",
                    "Running",
                    50.0));
    Result result = controller.list(customer.uuid);
    assertEquals(OK, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertTrue(json.isObject());
    JsonNode universeTasks = json.get(universe.universeUUID.toString());
    assertTrue(universeTasks.isArray());
    assertEquals(25, universeTasks.size());
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testTaskHistoryProgressCompletes() {
    String authToken = user.createAuthToken();
    UUID taskUUID =
        createTaskWithStatus(
            universe.universeUUID,
            CustomerTask.TargetType.Universe,
            Create,
            TaskType.CreateUniverse,
            "Foo",
            "Success",
            100.0);
    Result result =
        FakeApiHelper.doRequestWithAuthToken(
            "GET", "/api/customers/" + customer.uuid + "/tasks", authToken);
    CustomerTask ct =
        CustomerTask.find.query().where().eq("task_uuid", taskUUID.toString()).findOne();
    assertEquals(OK, result.status());
    assertThat(
        contentAsString(result), allOf(notNullValue(), containsString("Created Universe : Foo")));
    assertTrue(ct.getCreateTime().before(ct.getCompletionTime()));
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testTaskStatusWithValidUUID() {
    String authToken = user.createAuthToken();
    ObjectNode responseJson = Json.newObject();
    UUID taskUUID =
        createTaskWithStatusAndResponse(
            universe.universeUUID,
            CustomerTask.TargetType.Universe,
            Create,
            TaskType.CreateUniverse,
            "Foo",
            "Success",
            100.0,
            responseJson);
    createSubTaskWithResponse(
        taskUUID, 0, TaskType.AnsibleSetupServer, TaskInfo.State.Success, responseJson);
    when(mockCommissioner.getStatusOrBadRequest(taskUUID)).thenReturn(responseJson);
    Result result =
        FakeApiHelper.doRequestWithAuthToken(
            "GET", "/api/customers/" + customer.uuid + "/tasks/" + taskUUID, authToken);

    assertEquals(OK, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertThat(json.get("status").asText(), allOf(notNullValue(), equalTo("Success")));
    assertThat(json.get("percent").asDouble(), allOf(notNullValue(), equalTo(100.0)));
    assertThat(json.get("title").asText(), is(notNullValue()));
    assertThat(json.get("createTime").asText(), is(notNullValue()));
    assertThat(json.get("target").asText(), allOf(notNullValue(), equalTo("Foo")));
    assertThat(json.get("type").asText(), allOf(notNullValue(), equalTo("Create")));
    assertValue(json, "targetUUID", universe.universeUUID.toString());
    assertThat(json.get("details"), is(notNullValue()));
    JsonNode taskDetailsJson = json.get("details").get("taskDetails");
    assertThat(taskDetailsJson, is(notNullValue()));
    assertTrue(taskDetailsJson.isArray());
    assertThat(
        taskDetailsJson.get(0).get("title").asText(),
        allOf(notNullValue(), equalTo("Configuring the universe")));
    assertThat(
        taskDetailsJson.get(0).get("state").asText(), allOf(notNullValue(), equalTo("Success")));
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testTaskStatusWithInvalidTaskUUID() {
    String authToken = user.createAuthToken();
    UUID taskUUID = UUID.randomUUID();
    Result result =
        assertPlatformException(
            () ->
                FakeApiHelper.doRequestWithAuthToken(
                    "GET", "/api/customers/" + customer.uuid + "/tasks/" + taskUUID, authToken));

    assertEquals(BAD_REQUEST, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertThat(
        json.get("error").asText(),
        allOf(notNullValue(), equalTo("Invalid Customer Task UUID: " + taskUUID)));
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testTaskStatusWithInvalidCustomerUUID() {
    String authToken = user.createAuthToken();
    UUID taskUUID = UUID.randomUUID();
    UUID customerUUID = UUID.randomUUID();
    Result result =
        FakeApiHelper.doRequestWithAuthToken(
            "GET", "/api/customers/" + customerUUID + "/tasks/" + taskUUID, authToken);

    assertEquals(FORBIDDEN, result.status());

    String resultString = contentAsString(result);
    assertThat(resultString, allOf(notNullValue(), equalTo("Unable To Authenticate User")));
    assertAuditEntry(0, customer.uuid);
  }
}
