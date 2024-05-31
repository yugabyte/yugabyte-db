// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.AssertHelper.assertAuditEntry;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static com.yugabyte.yw.common.AssertHelper.assertValue;
import static com.yugabyte.yw.common.AssertHelper.assertValues;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static com.yugabyte.yw.models.CustomerTask.TaskType.Create;
import static com.yugabyte.yw.models.CustomerTask.TaskType.GFlagsUpgrade;
import static com.yugabyte.yw.models.CustomerTask.TaskType.TlsToggle;
import static com.yugabyte.yw.models.CustomerTask.TaskType.Update;
import static org.hamcrest.CoreMatchers.allOf;
import static org.hamcrest.CoreMatchers.containsString;
import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.is;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.when;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.OK;
import static play.mvc.Http.Status.UNAUTHORIZED;
import static play.test.Helpers.contentAsString;
import static play.test.Helpers.fakeRequest;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.common.config.CustomerConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
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
import java.util.Collections;
import java.util.Optional;
import java.util.UUID;
import java.util.concurrent.TimeUnit;
import java.util.stream.IntStream;
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

  @Mock RuntimeConfGetter mockConfGetter;

  @InjectMocks private CustomerTaskController controller;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    user = ModelFactory.testUser(customer);
    universe = createUniverse(customer.getId());
  }

  @Test
  public void testTaskHistoryEmptyList() {
    String authToken = user.createAuthToken();
    Result result =
        route(
            fakeRequest("GET", "/api/customers/" + customer.getUuid() + "/tasks")
                .header("X-AUTH-TOKEN", authToken));

    assertThat(result.status(), is(OK));

    JsonNode json = Json.parse(contentAsString(result));
    assertThat(json.isObject(), is(true));
    assertThat(json.size(), is(0));
    assertAuditEntry(0, customer.getUuid());
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
    CustomerTask task =
        createTaskWithStatusAndResponse(
            targetUUID,
            targetType,
            taskType,
            taskInfoType,
            targetName,
            status,
            percentComplete,
            responseJson);
    TaskInfo taskInfo = TaskInfo.getOrBadRequest(task.getTaskUUID());
    when(mockCommissioner.buildTaskStatus(eq(task), eq(taskInfo), any(), any()))
        .thenReturn(Optional.of(responseJson));
    when(mockCommissioner.getUpdatingTaskUUIDsForTargets(any())).thenReturn(Collections.emptyMap());
    return task.getTaskUUID();
  }

  private CustomerTask createTaskWithStatusAndResponse(
      UUID targetUUID,
      CustomerTask.TargetType targetType,
      CustomerTask.TaskType taskType,
      TaskType taskInfoType,
      String targetName,
      String status,
      double percentComplete,
      ObjectNode responseJson) {
    return createTaskWithStatusAndResponse(
        targetUUID,
        targetType,
        taskType,
        taskInfoType,
        targetName,
        status,
        percentComplete,
        null,
        responseJson);
  }

  private CustomerTask createTaskWithStatusAndResponse(
      UUID targetUUID,
      CustomerTask.TargetType targetType,
      CustomerTask.TaskType taskType,
      TaskType taskInfoType,
      String targetName,
      String status,
      double percentComplete,
      String customTypeName,
      ObjectNode responseJson) {
    UUID taskUUID = UUID.randomUUID();
    // Set http context
    TestUtils.setFakeHttpContext(user);
    TaskInfo taskInfo = new TaskInfo(taskInfoType, null);
    taskInfo.setTaskUUID(taskUUID);
    taskInfo.setTaskParams(Json.newObject());
    taskInfo.setOwner("");
    taskInfo.save();
    CustomerTask task =
        CustomerTask.create(
            customer, targetUUID, taskUUID, targetType, taskType, targetName, customTypeName);
    responseJson.put("status", status);
    responseJson.put("percent", percentComplete);
    responseJson.put("title", task.getFriendlyDescription());
    responseJson.put("createTime", task.getCreateTime().toString());
    responseJson.put("target", targetName);
    responseJson.put("targetUUID", targetUUID.toString());
    responseJson.put("type", taskType.name());
    responseJson.put("typeName", taskType.getFriendlyName());
    responseJson.put("abortable", false);
    responseJson.put("retryable", false);
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
    return task;
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
    TaskInfo subTask = new TaskInfo(taskType, null);
    subTask.setParentUuid(parentUUID);
    subTask.setPosition(position);
    subTask.setSubTaskGroupType(groupType);
    subTask.setTaskState(taskState);
    ObjectNode taskDetailsJson = Json.newObject();
    taskDetailsJson.put("errorString", taskState.equals(TaskInfo.State.Failure) ? "foobaz" : null);
    subTask.setTaskParams(taskDetailsJson);
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
            CustomerTask.TaskType.SoftwareUpgradeYB,
            TaskType.SoftwareUpgrade,
            "Foo",
            "Success",
            100.0);

    // update task details with version numbers
    ObjectMapper objectMapper = new ObjectMapper();
    ObjectNode objectNode = objectMapper.createObjectNode();
    TaskInfo taskInfo = TaskInfo.get(upgradeUUID);
    JsonNode taskParams = taskInfo.getTaskParams();
    objectNode.setAll((ObjectNode) taskParams);
    objectNode.put(YB_SOFTWARE_VERSION, "{Previous Version}");
    objectNode.put(YB_PREV_SOFTWARE_VERSION, "{Current Version}");
    taskInfo.setTaskParams(objectNode);
    taskInfo.save();

    String url = "/api/customers/" + customer.getUuid() + "/tasks";
    Result result = doRequestWithAuthToken("GET", url, authToken);
    JsonNode json = Json.parse(contentAsString(result));
    assertThat(result.status(), is(OK));
    assertThat(json.isObject(), is(true));
    JsonNode universeTasks = json.get(universeUUID.toString());
    JsonNode upgradeTask = universeTasks.get(0);
    taskParams = taskInfo.getTaskParams();
    assertThat(
        ((upgradeTask.get("type").asText().equals("SoftwareUpgradeYB")
                && taskParams.has(YB_PREV_SOFTWARE_VERSION)))
            || (!upgradeTask.get("type").asText().equals("SoftwareUpgradeYB")
                && !taskParams.has(YB_SOFTWARE_VERSION)),
        is(true));
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

    String url = "/api/customers/" + customer.getUuid() + "/tasks/" + taskUUID + "/failed";
    Result result = doRequestWithAuthToken("GET", url, authToken);
    assertThat(result.status(), is(OK));
    JsonNode json = Json.parse(contentAsString(result));
    assertThat(json.isObject(), is(true));
    JsonNode failedSubTasks = json.get("failedSubTasks");
    assertThat(failedSubTasks.isArray(), is(true));
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
    assertAuditEntry(0, customer.getUuid());
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

    String url = "/api/customers/" + customer.getUuid() + "/tasks";
    Result result = doRequestWithAuthToken("GET", url, authToken);
    assertThat(result.status(), is(OK));
    JsonNode json = Json.parse(contentAsString(result));
    assertThat(json.isObject(), is(true));
    assertThat(json.size(), is(2));
    JsonNode universeTasks = json.get(universeUUID.toString());
    assertThat(universeTasks.isArray(), is(true));
    assertThat(universeTasks.size(), is(1));
    assertValues(universeTasks, "id", ImmutableList.of(taskUUID.toString()));
    JsonNode task = universeTasks.get(0);
    assertThat(
        task.get("title").asText(), allOf(notNullValue(), equalTo("Creating Universe : Foo")));
    assertThat(task.get("percentComplete").asDouble(), allOf(notNullValue(), equalTo(50.0)));
    assertThat(task.get("status").asText(), allOf(notNullValue(), equalTo("Running")));
    assertThat(
        task.get("createTime").asLong() < Calendar.getInstance().getTimeInMillis(), is(true));
    assertThat(task.has("completionTime"), is(false));
    assertThat(task.get("target").asText(), allOf(notNullValue(), equalTo("Universe")));
    assertThat(
        task.get("targetUUID").asText(), allOf(notNullValue(), equalTo(universeUUID.toString())));
    JsonNode providerTasks = json.get(providerUUID.toString());
    assertThat(providerTasks.isArray(), is(true));
    assertThat(providerTasks.size(), is(2));
    assertValues(
        providerTasks,
        "id",
        ImmutableList.of(providerTaskUUID1.toString(), providerTaskUUID2.toString()));
    assertAuditEntry(0, customer.getUuid());
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

    String url = "/api/customers/" + customer.getUuid() + "/tasks_list";
    Result result = doRequestWithAuthToken("GET", url, authToken);
    assertThat(result.status(), is(OK));
    JsonNode universeTasks = Json.parse(contentAsString(result));

    assertThat(universeTasks.isArray(), is(true));
    assertThat(universeTasks.size(), is(1));
    JsonNode task = universeTasks.get(0);
    assertThat(
        task.get("title").asText(), allOf(notNullValue(), equalTo("Updating Provider : Foo")));
    assertThat(task.get("percentComplete").asDouble(), allOf(notNullValue(), equalTo(10.0)));
    assertThat(task.get("status").asText(), allOf(notNullValue(), equalTo("Running")));
    assertThat(
        task.get("createTime").asLong() < Calendar.getInstance().getTimeInMillis(), is(true));
    assertThat(task.has("completionTime"), is(false));
    assertThat(task.get("target").asText(), allOf(notNullValue(), equalTo("Provider")));
    assertThat(
        task.get("targetUUID").asText(), allOf(notNullValue(), equalTo(providerUUID.toString())));
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testTaskListWithUniverseUUID() {
    String authToken = user.createAuthToken();
    Universe universe1 = createUniverse("Universe 2", customer.getId());

    UUID taskUUID1 =
        createTaskWithStatus(
            universe.getUniverseUUID(),
            CustomerTask.TargetType.Universe,
            Create,
            TaskType.CreateUniverse,
            "Foo",
            "Running",
            50.0);
    createTaskWithStatus(
        universe1.getUniverseUUID(),
        CustomerTask.TargetType.Universe,
        Create,
        TaskType.CreateUniverse,
        "Bar",
        "Running",
        90.0);
    String url =
        "/api/customers/" + customer.getUuid() + "/tasks_list?uUUID=" + universe.getUniverseUUID();
    Result result = doRequestWithAuthToken("GET", url, authToken);
    assertThat(result.status(), is(OK));
    JsonNode universeTasks = Json.parse(contentAsString(result));

    assertThat(universeTasks.isArray(), is(true));
    assertThat(universeTasks.size(), is(1));
    assertValues(universeTasks, "id", ImmutableList.of(taskUUID1.toString()));
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testFriendlyNames() {
    String authToken = user.createAuthToken();
    UUID taskUUID =
        createTaskWithStatus(
            universe.getUniverseUUID(),
            CustomerTask.TargetType.Universe,
            GFlagsUpgrade,
            TaskType.GFlagsUpgrade,
            "Foo",
            "Success",
            100.0);
    Result result =
        doRequestWithAuthToken("GET", "/api/customers/" + customer.getUuid() + "/tasks", authToken);
    CustomerTask.find.query().where().eq("task_uuid", taskUUID.toString()).findOne();
    assertThat(result.status(), is(OK));
    JsonNode json = Json.parse(contentAsString(result));
    JsonNode universeTasks = json.get(universe.getUniverseUUID().toString());
    assertThat(universeTasks.isArray(), is(true));
    JsonNode task = universeTasks.get(0);
    assertThat(task.get("typeName").asText(), equalTo("GFlags Upgrade"));
  }

  @Test
  public void testTaskCompletionTime() {
    String authToken = user.createAuthToken();
    createTaskWithStatus(
        universe.getUniverseUUID(),
        CustomerTask.TargetType.Universe,
        Create,
        TaskType.CreateUniverse,
        "Foo",
        "Success",
        100.0);

    String markedCompletionTime = null;
    for (int idx = 0; idx < 2; idx++) {
      String url = "/api/customers/" + customer.getUuid() + "/tasks";
      Result result = doRequestWithAuthToken("GET", url, authToken);
      assertThat(result.status(), is(OK));
      assertAuditEntry(0, customer.getUuid());
      JsonNode tasksJson = Json.parse(contentAsString(result));
      JsonNode universeTasks = tasksJson.get(universe.getUniverseUUID().toString());
      if (idx == 0) {
        markedCompletionTime = universeTasks.get(0).get("completionTime").asText();
        try {
          Thread.sleep(1000);
        } catch (InterruptedException e) {
          e.printStackTrace();
        }
      } else {
        assertThat(markedCompletionTime, is(universeTasks.get(0).get("completionTime").asText()));
      }
    }
  }

  @Test
  public void testTaskHistoryUniverseList() {
    String authToken = user.createAuthToken();
    Universe universe1 = createUniverse("Universe 2", customer.getId());

    UUID taskUUID1 =
        createTaskWithStatus(
            universe.getUniverseUUID(),
            CustomerTask.TargetType.Universe,
            Create,
            TaskType.CreateUniverse,
            "Foo",
            "Running",
            50.0);
    createTaskWithStatus(
        universe1.getUniverseUUID(),
        CustomerTask.TargetType.Universe,
        Create,
        TaskType.CreateUniverse,
        "Bar",
        "Running",
        90.0);
    Result result =
        doRequestWithAuthToken(
            "GET",
            "/api/customers/"
                + customer.getUuid()
                + "/universes/"
                + universe.getUniverseUUID()
                + "/tasks",
            authToken);
    assertThat(result.status(), is(OK));
    JsonNode json = Json.parse(contentAsString(result));
    assertThat(json.isObject(), is(true));
    JsonNode universeTasks = json.get(universe.getUniverseUUID().toString());
    assertThat(universeTasks.isArray(), is(true));
    assertThat(universeTasks.size(), is(1));
    assertValues(universeTasks, "id", ImmutableList.of(taskUUID1.toString()));
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testTaskHistoryLimit() {
    when(mockConfGetter.getConfForScope(any(Customer.class), eq(CustomerConfKeys.taskDbQueryLimit)))
        .thenReturn(25);
    IntStream.range(0, 100)
        .forEach(
            i ->
                createTaskWithStatus(
                    universe.getUniverseUUID(),
                    CustomerTask.TargetType.Universe,
                    Create,
                    TaskType.CreateUniverse,
                    "Foo",
                    "Running",
                    50.0));
    Result result = controller.list(customer.getUuid());
    assertThat(result.status(), is(OK));
    JsonNode json = Json.parse(contentAsString(result));
    assertThat(json.isObject(), is(true));
    JsonNode universeTasks = json.get(universe.getUniverseUUID().toString());
    assertThat(universeTasks.isArray(), is(true));
    assertThat(universeTasks.size(), is(25));
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testTaskHistoryProgressCompletes() {
    String authToken = user.createAuthToken();
    UUID taskUUID =
        createTaskWithStatus(
            universe.getUniverseUUID(),
            CustomerTask.TargetType.Universe,
            Create,
            TaskType.CreateUniverse,
            "Foo",
            "Success",
            100.0);
    Result result =
        doRequestWithAuthToken("GET", "/api/customers/" + customer.getUuid() + "/tasks", authToken);
    CustomerTask ct =
        CustomerTask.find.query().where().eq("task_uuid", taskUUID.toString()).findOne();
    assertThat(result.status(), is(OK));
    assertThat(
        contentAsString(result), allOf(notNullValue(), containsString("Created Universe : Foo")));
    assertThat(ct.getCreateTime().before(ct.getCompletionTime()), is(true));
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testTaskStatusWithValidUUID() {
    String authToken = user.createAuthToken();
    ObjectNode responseJson = Json.newObject();
    CustomerTask task =
        createTaskWithStatusAndResponse(
            universe.getUniverseUUID(),
            CustomerTask.TargetType.Universe,
            Create,
            TaskType.CreateUniverse,
            "Foo",
            "Success",
            100.0,
            responseJson);
    UUID taskUUID = task.getTaskUUID();
    createSubTaskWithResponse(
        taskUUID, 0, TaskType.AnsibleSetupServer, TaskInfo.State.Success, responseJson);
    when(mockCommissioner.getStatusOrBadRequest(taskUUID)).thenReturn(responseJson);
    Result result =
        doRequestWithAuthToken(
            "GET", "/api/customers/" + customer.getUuid() + "/tasks/" + taskUUID, authToken);

    assertThat(result.status(), is(OK));
    JsonNode json = Json.parse(contentAsString(result));
    assertThat(json.get("status").asText(), allOf(notNullValue(), equalTo("Success")));
    assertThat(json.get("percent").asDouble(), allOf(notNullValue(), equalTo(100.0)));
    assertThat(json.get("title").asText(), is(notNullValue()));
    assertThat(json.get("createTime").asText(), is(notNullValue()));
    assertThat(json.get("target").asText(), allOf(notNullValue(), equalTo("Foo")));
    assertThat(json.get("type").asText(), allOf(notNullValue(), equalTo("Create")));
    assertValue(json, "targetUUID", universe.getUniverseUUID().toString());
    assertThat(json.get("details"), is(notNullValue()));
    JsonNode taskDetailsJson = json.get("details").get("taskDetails");
    assertThat(taskDetailsJson, is(notNullValue()));
    assertThat(taskDetailsJson.isArray(), is(true));
    assertThat(
        taskDetailsJson.get(0).get("title").asText(),
        allOf(notNullValue(), equalTo("Configuring the universe")));
    assertThat(
        taskDetailsJson.get(0).get("state").asText(), allOf(notNullValue(), equalTo("Success")));
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testTaskStatusWithInvalidTaskUUID() {
    String authToken = user.createAuthToken();
    UUID taskUUID = UUID.randomUUID();
    Result result =
        assertPlatformException(
            () ->
                doRequestWithAuthToken(
                    "GET",
                    "/api/customers/" + customer.getUuid() + "/tasks/" + taskUUID,
                    authToken));

    assertThat(result.status(), is(BAD_REQUEST));
    JsonNode json = Json.parse(contentAsString(result));
    assertThat(
        json.get("error").asText(),
        allOf(notNullValue(), equalTo("Invalid Customer Task UUID: " + taskUUID)));
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testTaskStatusWithInvalidCustomerUUID() {
    String authToken = user.createAuthToken();
    UUID taskUUID = UUID.randomUUID();
    UUID customerUUID = UUID.randomUUID();
    Result result =
        doRequestWithAuthToken(
            "GET", "/api/customers/" + customerUUID + "/tasks/" + taskUUID, authToken);

    assertThat(result.status(), is(UNAUTHORIZED));

    String resultString = contentAsString(result);
    assertThat(resultString, allOf(notNullValue(), equalTo("Unable To Authenticate User")));
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testCustomTaskTypeName() {
    String authToken = user.createAuthToken();
    ObjectNode responseJson = Json.newObject();
    createTaskWithStatusAndResponse(
        universe.getUniverseUUID(),
        CustomerTask.TargetType.Universe,
        TlsToggle,
        TaskType.TlsToggle,
        "Foo",
        "Success",
        99.0,
        "TLS Toggle ON",
        responseJson);
    when(mockCommissioner.buildTaskStatus(any(), any(), any(), any()))
        .thenReturn(Optional.of(responseJson));
    Result result =
        doRequestWithAuthToken("GET", "/api/customers/" + customer.getUuid() + "/tasks", authToken);
    assertThat(result.status(), is(OK));
    JsonNode json = Json.parse(contentAsString(result));
    JsonNode universeTasks = json.get(universe.getUniverseUUID().toString());
    assertThat(universeTasks.isArray(), is(true));
    JsonNode task = universeTasks.get(0);
    assertThat(task.get("typeName").asText(), equalTo("TLS Toggle ON"));
  }
}
