// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.common.FakeApiHelper;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;

import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import org.junit.Before;
import org.junit.Test;
import play.Application;
import play.inject.guice.GuiceApplicationBuilder;
import play.libs.Json;
import play.mvc.Result;
import play.test.WithApplication;
import play.test.Helpers;

import java.util.Calendar;
import java.util.Map;
import java.util.UUID;

import static com.yugabyte.yw.common.AssertHelper.assertValues;
import static com.yugabyte.yw.models.CustomerTask.TaskType.Create;
import static com.yugabyte.yw.models.CustomerTask.TaskType.Update;
import static org.hamcrest.CoreMatchers.*;
import static org.hamcrest.CoreMatchers.equalTo;
import static org.junit.Assert.*;
import static org.junit.Assert.assertEquals;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;
import static play.inject.Bindings.bind;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.contentAsString;
import static play.test.Helpers.fakeRequest;
import static play.test.Helpers.route;

public class CustomerTaskControllerTest extends WithApplication {
  private Customer customer;
  private Universe universe;
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
  public void setUp() {
    customer = ModelFactory.testCustomer();
    universe = Universe.create("Test Universe", UUID.randomUUID(), customer.getCustomerId());
  }

  @Test
  public void testTaskHistoryEmptyList() {
    String authToken = customer.createAuthToken();
    Result result = route(fakeRequest("GET", "/api/customers/" + customer.uuid + "/tasks")
                            .header("X-AUTH-TOKEN", authToken));

    assertEquals(OK, result.status());

    JsonNode json = Json.parse(contentAsString(result));
    assertTrue(json.isObject());
    assertEquals(0, json.size());
  }

  private UUID createTaskWithStatus(UUID targetUUID, CustomerTask.TargetType targetType,
                                    CustomerTask.TaskType taskType, String targetName,
                                    String status, int percentComplete) {
    UUID taskUUID = UUID.randomUUID();
    CustomerTask.create(customer, targetUUID, taskUUID, targetType, taskType, targetName);

    ObjectNode responseJson = Json.newObject();
    responseJson.put("status", status);
    responseJson.put("percent", percentComplete);
    when(mockCommissioner.getStatus(taskUUID)).thenReturn(responseJson);
    return taskUUID;
  }

  private UUID createSubTask(UUID parentUUID, int position, TaskType taskType,
                             TaskInfo.State taskState) {
    TaskInfo subTask = new TaskInfo(taskType);
    subTask.setParentUuid(parentUUID);
    subTask.setPosition(position);
    subTask.setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);
    subTask.setTaskState(taskState);
    subTask.setTaskDetails(Json.newObject());
    subTask.setOwner("foobar");
    subTask.save();
    return subTask.getTaskUUID();
  }

  @Test
  public void testFetchTaskWithFailedSubtasks() {
    String authToken = customer.createAuthToken();
    UUID universeUUID = UUID.randomUUID();
    UUID taskUUID = createTaskWithStatus(universeUUID, CustomerTask.TargetType.Universe, Create,
        "Foo", "Failed", 50);
    UUID subTaskUUID = createSubTask(taskUUID, 0, TaskType.AnsibleSetupServer,
        TaskInfo.State.Failure);

    String url = "/api/customers/" + customer.uuid + "/tasks/" + taskUUID + "/failed";
    Result result = FakeApiHelper.doRequestWithAuthToken("GET", url, authToken);
    assertEquals(OK, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertTrue(json.isObject());
    JsonNode failedSubTasks = json.get("failedSubTasks");
    assertTrue(failedSubTasks.isArray());
    assertThat(failedSubTasks.get(0).get("subTaskUUID").asText(), allOf(notNullValue(),
        equalTo(subTaskUUID.toString())));
    assertThat(failedSubTasks.get(0).get("subTaskType").asText(), allOf(notNullValue(),
        equalTo(TaskType.AnsibleSetupServer.name())));
    assertThat(failedSubTasks.get(0).get("subTaskState").asText(), allOf(notNullValue(),
        equalTo(TaskInfo.State.Failure.toString())));
    assertThat(failedSubTasks.get(0).get("subTaskGroupType").asText(), allOf(notNullValue(),
        equalTo(UserTaskDetails.SubTaskGroupType.ConfigureUniverse.name())));
    assertThat(failedSubTasks.get(0).get("creationTime").asText(), is(notNullValue()));
  }

  @Test
  public void testTaskHistoryList() {
    String authToken = customer.createAuthToken();
    UUID universeUUID = UUID.randomUUID();
    UUID universeTaskUUID = createTaskWithStatus(universeUUID, CustomerTask.TargetType.Universe,
        Create, "Foo", "Running", 50);

    UUID providerUUID = UUID.randomUUID();
    UUID providerTaskUUID1 = createTaskWithStatus(providerUUID, CustomerTask.TargetType.Provider,
        Create, "Foo", "Success", 100);
    UUID providerTaskUUID2 = createTaskWithStatus(providerUUID, CustomerTask.TargetType.Provider,
        Update, "Foo", "Running", 10);

    Result result = FakeApiHelper.doRequestWithAuthToken("GET", "/api/customers/" + customer.uuid + "/tasks", authToken);
    assertEquals(OK, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertTrue(json.isObject());
    assertEquals(2, json.size());
    JsonNode universeTasks = json.get(universeUUID.toString());
    assertTrue(universeTasks.isArray());
    assertEquals(1, universeTasks.size());
    assertValues(universeTasks, "id", ImmutableList.of(universeTaskUUID.toString()));
    assertThat(universeTasks.get(0).get("title").asText(), allOf(notNullValue(), equalTo("Creating Universe : Foo")));
    assertThat(universeTasks.get(0).get("percentComplete").asInt(), allOf(notNullValue(), equalTo(50)));
    assertThat(universeTasks.get(0).get("status").asText(), allOf(notNullValue(), equalTo("Running")));
    assertTrue(universeTasks.get(0).get("createTime").asLong() < Calendar.getInstance().getTimeInMillis());
    assertTrue(universeTasks.get(0).get("completionTime").isNull());
    assertThat(universeTasks.get(0).get("target").asText(), allOf(notNullValue(), equalTo("Universe")));

    JsonNode providerTasks = json.get(providerUUID.toString());
    assertTrue(providerTasks.isArray());
    assertEquals(2, providerTasks.size());
    assertValues(providerTasks, "id", ImmutableList.of(providerTaskUUID1.toString(),
        providerTaskUUID2.toString()));
  }

  @Test
  public void testTaskCompletionTime() {
    String authToken = customer.createAuthToken();
    createTaskWithStatus(universe.universeUUID, CustomerTask.TargetType.Universe,
        Create, "Foo", "Success", 100);

    String markedCompletionTime = null;
    for (int idx = 0; idx < 2; idx++) {
      Result result = FakeApiHelper.doRequestWithAuthToken("GET", "/api/customers/" + customer.uuid + "/tasks", authToken);
      assertEquals(OK, result.status());
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
    String authToken = customer.createAuthToken();
    Universe universe1 = Universe.create("Universe 2", UUID.randomUUID(), customer.getCustomerId());

    UUID taskUUID1 = createTaskWithStatus(universe.universeUUID, CustomerTask.TargetType.Universe,
        Create, "Foo", "Running", 50);
    createTaskWithStatus(universe1.universeUUID, CustomerTask.TargetType.Universe,
        Create, "Bar", "Running", 90);
    Result result = FakeApiHelper.doRequestWithAuthToken("GET", "/api/customers/" +
        customer.uuid +  "/universes/" + universe.universeUUID + "/tasks", authToken);
    assertEquals(OK, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertTrue(json.isObject());
    JsonNode universeTasks = json.get(universe.universeUUID.toString());
    assertTrue(universeTasks.isArray());
    assertEquals(1, universeTasks.size());
    assertValues(universeTasks, "id", ImmutableList.of(taskUUID1.toString()));
  }

  @Test
  public void testTaskHistoryProgressCompletes() {
    String authToken = customer.createAuthToken();
    UUID taskUUID = createTaskWithStatus(universe.universeUUID, CustomerTask.TargetType.Universe,
        Create, "Foo", "Success", 100);
    Result result = FakeApiHelper.doRequestWithAuthToken("GET", "/api/customers/" +
        customer.uuid + "/tasks", authToken);
    CustomerTask ct = CustomerTask.find.where().eq("task_uuid", taskUUID.toString()).findUnique();
    assertEquals(OK, result.status());
    assertThat(contentAsString(result), allOf(notNullValue(), containsString("Created Universe : Foo")));
    assertTrue(ct.getCreateTime().before(ct.getCompletionTime()));
  }

  @Test
  public void testTaskStatusWithValidUUID() {
    String authToken = customer.createAuthToken();
    UUID taskUUID = createTaskWithStatus(universe.universeUUID, CustomerTask.TargetType.Universe,
        Create, "Foo", "Success", 100);
    Result result = FakeApiHelper.doRequestWithAuthToken("GET", "/api/customers/" +
        customer.uuid + "/tasks/" + taskUUID, authToken);

    assertEquals(OK, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertThat(json.get("status").asText(), allOf(notNullValue(), equalTo("Success")));
    assertThat(json.get("percent").asInt(), allOf(notNullValue(), equalTo(100)));
  }

  @Test
  public void testTaskStatusWithInvalidTaskUUID() {
    String authToken = customer.createAuthToken();
    UUID taskUUID = UUID.randomUUID();

    Result result = FakeApiHelper.doRequestWithAuthToken("GET", "/api/customers/" +
        customer.uuid + "/tasks/" + taskUUID, authToken);

    assertEquals(BAD_REQUEST, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertThat(json.get("error").asText(), allOf(notNullValue(), equalTo("Invalid Customer Task UUID: " + taskUUID)));
  }

  @Test
  public void testTaskStatusWithInvalidCustomerUUID() {
    String authToken = customer.createAuthToken();
    UUID taskUUID = UUID.randomUUID();
    UUID customerUUID = UUID.randomUUID();
    Result result = FakeApiHelper.doRequestWithAuthToken("GET", "/api/customers/" +
        customerUUID + "/tasks/" + taskUUID, authToken);

    assertEquals(BAD_REQUEST, result.status());

    JsonNode json = Json.parse(contentAsString(result));
    assertThat(json.get("error").asText(), allOf(notNullValue(), equalTo("Invalid Customer UUID: " + customerUUID)));
  }
}
