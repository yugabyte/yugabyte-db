// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.AssertHelper.assertAuditEntry;
import static com.yugabyte.yw.common.AssertHelper.assertBadRequest;
import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyLong;
import static org.mockito.Mockito.doThrow;
import static org.mockito.Mockito.when;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.UNAUTHORIZED;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.BackupRequestParams;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.forms.EditBackupScheduleParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.Schedule.State;
import com.yugabyte.yw.models.ScheduleTask;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.models.helpers.TimeUnit;
import java.util.List;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import play.libs.Json;
import play.mvc.Result;

public class ScheduleControllerTest extends FakeDBApplication {

  private Universe defaultUniverse;
  private Customer defaultCustomer;
  private CustomerConfig customerConfig;
  private Users defaultUser;
  private Schedule defaultSchedule;
  private Schedule defaultIncrementalSchedule;
  private BackupTableParams backupTableParams;
  private BackupRequestParams backupRequestParams;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    defaultUser = ModelFactory.testUser(defaultCustomer);
    defaultUniverse = ModelFactory.createUniverse(defaultCustomer.getId());

    backupTableParams = new BackupTableParams();
    backupTableParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST16");
    backupTableParams.storageConfigUUID = customerConfig.getConfigUUID();
    defaultSchedule =
        Schedule.create(
            defaultCustomer.getUuid(), backupTableParams, TaskType.BackupUniverse, 1000, null);
  }

  private Result listSchedules(UUID customerUUID) {
    String authToken = defaultUser.createAuthToken();
    String method = "GET";
    String url = "/api/customers/" + customerUUID + "/schedules";

    return doRequestWithAuthToken(method, url, authToken);
  }

  private Result deleteSchedule(UUID scheduleUUID, UUID customerUUID) {
    String authToken = defaultUser.createAuthToken();
    String method = "DELETE";
    String url = "/api/customers/" + customerUUID + "/schedules/" + scheduleUUID;

    return doRequestWithAuthToken(method, url, authToken);
  }

  private Result deleteScheduleYb(UUID scheduleUUID, UUID customerUUID) {
    String authToken = defaultUser.createAuthToken();
    String method = "DELETE";
    String url = "/api/customers/" + customerUUID + "/schedules/" + scheduleUUID + "/delete";
    return doRequestWithAuthToken(method, url, authToken);
  }

  private Result editSchedule(UUID scheduleUUID, UUID customerUUID, JsonNode body) {
    String authToken = defaultUser.createAuthToken();
    String method = "PUT";
    String url = "/api/customers/" + customerUUID + "/schedules/" + scheduleUUID;

    return doRequestWithAuthTokenAndBody(method, url, authToken, body);
  }

  private Result getPagedSchedulesList(UUID customerUUID, JsonNode body) {
    String authToken = defaultUser.createAuthToken();
    String method = "POST";
    String url = "/api/customers/" + customerUUID + "/schedules/page";
    return doRequestWithAuthTokenAndBody(method, url, authToken, body);
  }

  private Result createBackupSchedule(ObjectNode bodyJson, Users user) {
    String authToken = user == null ? defaultUser.createAuthToken() : user.createAuthToken();
    String method = "POST";
    String url = "/api/customers/" + defaultCustomer.getUuid() + "/create_backup_schedule";
    return doRequestWithAuthTokenAndBody(method, url, authToken, bodyJson);
  }

  @Test
  public void testListWithValidCustomer() {
    Result r = listSchedules(defaultCustomer.getUuid());
    assertOk(r);
    JsonNode resultJson = Json.parse(contentAsString(r));
    assertEquals(1, resultJson.size());
    assertEquals(
        resultJson.get(0).get("scheduleUUID").asText(),
        defaultSchedule.getScheduleUUID().toString());
    assertAuditEntry(0, defaultCustomer.getUuid());
  }

  @Test
  public void testListWithInvalidCustomer() {
    UUID invalidCustomerUUID = UUID.randomUUID();
    Result r = listSchedules(invalidCustomerUUID);
    assertEquals(UNAUTHORIZED, r.status());
    String resultString = contentAsString(r);
    assertEquals(resultString, "Unable To Authenticate User");
    assertAuditEntry(0, defaultCustomer.getUuid());
  }

  @Test
  public void testDeleteValid() {
    JsonNode resultJson = Json.parse(contentAsString(listSchedules(defaultCustomer.getUuid())));
    assertEquals(1, resultJson.size());
    Result r = deleteSchedule(defaultSchedule.getScheduleUUID(), defaultCustomer.getUuid());
    assertOk(r);
    resultJson = Json.parse(contentAsString(listSchedules(defaultCustomer.getUuid())));
    assertEquals(0, resultJson.size());
    assertAuditEntry(1, defaultCustomer.getUuid());
  }

  @Test
  public void testDeleteInvalidCustomerUUID() {
    UUID invalidCustomerUUID = UUID.randomUUID();
    JsonNode resultJson = Json.parse(contentAsString(listSchedules(defaultCustomer.getUuid())));
    assertEquals(1, resultJson.size());
    Result r = deleteSchedule(defaultSchedule.getScheduleUUID(), invalidCustomerUUID);
    assertEquals(UNAUTHORIZED, r.status());
    String resultString = contentAsString(r);
    assertEquals(resultString, "Unable To Authenticate User");
    resultJson = Json.parse(contentAsString(listSchedules(defaultCustomer.getUuid())));
    assertEquals(1, resultJson.size());
    assertAuditEntry(0, defaultCustomer.getUuid());
  }

  @Test
  public void testDeleteInvalidScheduleUUID() {
    UUID invalidScheduleUUID = UUID.randomUUID();
    JsonNode resultJson = Json.parse(contentAsString(listSchedules(defaultCustomer.getUuid())));
    assertEquals(1, resultJson.size());
    Result result =
        assertPlatformException(
            () -> deleteSchedule(invalidScheduleUUID, defaultCustomer.getUuid()));
    assertBadRequest(result, "Invalid Schedule UUID: " + invalidScheduleUUID);
    resultJson = Json.parse(contentAsString(listSchedules(defaultCustomer.getUuid())));
    assertEquals(1, resultJson.size());
    assertAuditEntry(0, defaultCustomer.getUuid());
  }

  @Test
  public void testEditScheduleUpdateFrequency() {
    EditBackupScheduleParams params = new EditBackupScheduleParams();
    params.frequency = 2 * 86400L * 1000L;
    params.status = State.Active;
    params.frequencyTimeUnit = TimeUnit.DAYS;
    JsonNode requestJson = Json.toJson(params);
    Result result =
        editSchedule(defaultSchedule.getScheduleUUID(), defaultCustomer.getUuid(), requestJson);
    assertOk(result);
    JsonNode resultJson = Json.parse(contentAsString(listSchedules(defaultCustomer.getUuid())));
    assertEquals(1, resultJson.size());
    assertEquals(
        resultJson.get(0).get("scheduleUUID").asText(),
        defaultSchedule.getScheduleUUID().toString());
    assertTrue(resultJson.get(0).get("frequency").asLong() == params.frequency);
    assertTrue(resultJson.get(0).get("status").asText().equals(params.status.name()));
    assertAuditEntry(1, defaultCustomer.getUuid());
  }

  @Test
  public void testEditIncrementalBackupScheduleFrequency() {
    backupRequestParams = new BackupRequestParams();
    backupRequestParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    backupRequestParams.storageConfigUUID = customerConfig.getConfigUUID();
    backupRequestParams.incrementalBackupFrequency = 1900 * 1000L;
    defaultIncrementalSchedule =
        Schedule.create(
            defaultCustomer.getUuid(),
            backupRequestParams,
            TaskType.CreateBackup,
            3600 * 1000L,
            null);
    EditBackupScheduleParams params = new EditBackupScheduleParams();
    params.frequency = 3600 * 1000L;
    params.status = State.Active;
    params.frequencyTimeUnit = TimeUnit.DAYS;
    params.incrementalBackupFrequency = 1800 * 1000L;
    params.incrementalBackupFrequencyTimeUnit = TimeUnit.DAYS;
    JsonNode requestJson = Json.toJson(params);
    Result result =
        assertPlatformException(
            () ->
                editSchedule(
                    defaultSchedule.getScheduleUUID(), defaultCustomer.getUuid(), requestJson));
    assertBadRequest(
        result, "Cannot assign incremental backup frequency to a non-incremental schedule");
    result =
        editSchedule(
            defaultIncrementalSchedule.getScheduleUUID(), defaultCustomer.getUuid(), requestJson);
    assertOk(result);
    Schedule schedule =
        Schedule.getOrBadRequest(
            defaultIncrementalSchedule.getCustomerUUID(),
            defaultIncrementalSchedule.getScheduleUUID());
    assertEquals(params.frequency.longValue(), schedule.getFrequency());
    assertEquals(params.status, schedule.getStatus());
    assertEquals(
        params.incrementalBackupFrequency.longValue(),
        schedule.getTaskParams().get("incrementalBackupFrequency").asLong());
    assertAuditEntry(1, defaultCustomer.getUuid());
  }

  @Test
  public void testEditScheduleUpdateFrequencyWithoutTimeUnit() {
    EditBackupScheduleParams params = new EditBackupScheduleParams();
    params.frequency = 2 * 86400L * 1000L;
    params.status = State.Active;
    JsonNode requestJson = Json.toJson(params);
    Result result =
        assertPlatformException(
            () ->
                editSchedule(
                    defaultSchedule.getScheduleUUID(), defaultCustomer.getUuid(), requestJson));
    assertBadRequest(result, "Please provide time unit for frequency");
  }

  @Test
  public void testEditIncrementalBackupScheduleFrequencyWithoutTimeUint() {
    backupRequestParams = new BackupRequestParams();
    backupRequestParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    backupRequestParams.storageConfigUUID = customerConfig.getConfigUUID();
    backupRequestParams.incrementalBackupFrequency = 1900 * 1000L;
    defaultIncrementalSchedule =
        Schedule.create(
            defaultCustomer.getUuid(),
            backupRequestParams,
            TaskType.CreateBackup,
            3600 * 1000L,
            null);
    EditBackupScheduleParams params = new EditBackupScheduleParams();
    params.frequency = 3600 * 1000L;
    params.status = State.Active;
    params.frequencyTimeUnit = TimeUnit.DAYS;
    params.incrementalBackupFrequency = 1800 * 1000L;
    JsonNode requestJson = Json.toJson(params);
    Result result =
        assertPlatformException(
            () ->
                editSchedule(
                    defaultIncrementalSchedule.getScheduleUUID(),
                    defaultCustomer.getUuid(),
                    requestJson));
    assertBadRequest(result, "Please provide time unit for incremental backup frequency");
  }

  @Test
  public void testEditInvalidIncrementalBackupScheduleFrequency() {
    doThrow(
            new PlatformServiceException(
                BAD_REQUEST,
                "Incremental backup frequency should be lower than full backup frequency."))
        .when(mockBackupHelper)
        .validateIncrementalScheduleFrequency(anyLong(), anyLong(), any());
    backupRequestParams = new BackupRequestParams();
    backupRequestParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    backupRequestParams.storageConfigUUID = customerConfig.getConfigUUID();
    backupRequestParams.incrementalBackupFrequency = 1900 * 1000L;
    defaultIncrementalSchedule =
        Schedule.create(
            defaultCustomer.getUuid(),
            backupRequestParams,
            TaskType.CreateBackup,
            3600 * 1000L,
            null);
    EditBackupScheduleParams params = new EditBackupScheduleParams();
    params.frequency = 3600 * 1000L;
    params.status = State.Active;
    params.frequencyTimeUnit = TimeUnit.DAYS;
    params.incrementalBackupFrequency = 180000 * 1000L;
    params.incrementalBackupFrequencyTimeUnit = TimeUnit.DAYS;
    JsonNode requestJson = Json.toJson(params);
    Result result =
        assertPlatformException(
            () ->
                editSchedule(
                    defaultIncrementalSchedule.getScheduleUUID(),
                    defaultCustomer.getUuid(),
                    requestJson));
    assertBadRequest(
        result, "Incremental backup frequency should be lower than full backup frequency.");
    params.cronExpression = "0 * * * *";
    result =
        assertPlatformException(
            () ->
                editSchedule(
                    defaultIncrementalSchedule.getScheduleUUID(),
                    defaultCustomer.getUuid(),
                    requestJson));
    assertBadRequest(
        result, "Incremental backup frequency should be lower than full backup frequency.");
  }

  @Test
  public void testEditScheduleUpdateCronExpression() {
    EditBackupScheduleParams params = new EditBackupScheduleParams();
    params.cronExpression = "0 12 * * *";
    params.status = State.Active;
    JsonNode requestJson = Json.toJson(params);
    Result result =
        editSchedule(defaultSchedule.getScheduleUUID(), defaultCustomer.getUuid(), requestJson);
    assertOk(result);
    JsonNode resultJson = Json.parse(contentAsString(listSchedules(defaultCustomer.getUuid())));
    assertEquals(1, resultJson.size());
    assertEquals(
        resultJson.get(0).get("scheduleUUID").asText(),
        defaultSchedule.getScheduleUUID().toString());
    assertEquals(resultJson.get(0).get("cronExpression").asText(), params.cronExpression);
    assertTrue(resultJson.get(0).get("status").asText().equals(params.status.name()));
    assertAuditEntry(1, defaultCustomer.getUuid());
  }

  @Test
  public void testEditScheduleUpdateCronExpressionWithStateAsStopped() {
    EditBackupScheduleParams params = new EditBackupScheduleParams();
    params.status = State.Stopped;
    params.frequency = 2 * 86400L * 1000L;
    JsonNode requestJson = Json.toJson(params);
    Result result =
        editSchedule(defaultSchedule.getScheduleUUID(), defaultCustomer.getUuid(), requestJson);
    assertOk(result);
    JsonNode resultJson = Json.parse(contentAsString(listSchedules(defaultCustomer.getUuid())));
    assertEquals(0, resultJson.size());
    assertAuditEntry(1, defaultCustomer.getUuid());
  }

  @Test
  public void testEditScheduleUpdateCronExpressionToThirtyMins() {
    EditBackupScheduleParams params = new EditBackupScheduleParams();
    params.cronExpression = "15,45 * * * *";
    params.status = State.Active;
    JsonNode requestJson = Json.toJson(params);
    Result result =
        assertPlatformException(
            () ->
                editSchedule(
                    defaultSchedule.getScheduleUUID(), defaultCustomer.getUuid(), requestJson));
    assertBadRequest(result, "Duration between the cron schedules cannot be less than 1 hour");
  }

  @Test
  public void testEditScheduleUpdateCronExpressionWithInvalidCron() {
    EditBackupScheduleParams params = new EditBackupScheduleParams();
    params.cronExpression = "15,45 * * * * *";
    params.status = State.Active;
    JsonNode requestJson = Json.toJson(params);
    Result result =
        assertPlatformException(
            () ->
                editSchedule(
                    defaultSchedule.getScheduleUUID(), defaultCustomer.getUuid(), requestJson));
    assertBadRequest(result, "Cron expression specified is invalid");
  }

  @Test
  public void testEditScheduleUpdateCronExpressionWithBothParams() {
    EditBackupScheduleParams params = new EditBackupScheduleParams();
    params.cronExpression = "15,45 * * * * *";
    params.status = State.Active;
    params.frequency = 2 * 86400L * 1000L;
    JsonNode requestJson = Json.toJson(params);
    Result result =
        assertPlatformException(
            () ->
                editSchedule(
                    defaultSchedule.getScheduleUUID(), defaultCustomer.getUuid(), requestJson));
    assertBadRequest(result, "Both schedule frequency and cron expression cannot be provided");
  }

  @Test
  public void testEditScheduleUpdateCronExpressionWithNoParamsProvided() {
    EditBackupScheduleParams params = new EditBackupScheduleParams();
    params.status = State.Active;
    JsonNode requestJson = Json.toJson(params);
    Result result =
        assertPlatformException(
            () ->
                editSchedule(
                    defaultSchedule.getScheduleUUID(), defaultCustomer.getUuid(), requestJson));
    assertBadRequest(result, "Both schedule frequency and cron expression cannot be null");
  }

  @Test
  public void testEditScheduleUpdateCronExpressionWithStateAsPaused() {
    EditBackupScheduleParams params = new EditBackupScheduleParams();
    params.status = State.Paused;
    params.frequency = 2 * 86400L * 1000L;
    JsonNode requestJson = Json.toJson(params);
    Result result =
        assertPlatformException(
            () ->
                editSchedule(
                    defaultSchedule.getScheduleUUID(), defaultCustomer.getUuid(), requestJson));
    assertBadRequest(
        result, "State paused is an internal state and cannot be specified by the user");
  }

  @Test
  public void testDeleteValidScheduleHavingTaskYb() {
    Schedule schedule =
        Schedule.create(
            defaultCustomer.getUuid(), backupTableParams, TaskType.BackupUniverse, 1000, null);
    UUID randomTaskUUID = UUID.randomUUID();
    ScheduleTask.create(randomTaskUUID, schedule.getScheduleUUID());
    Result r = deleteScheduleYb(schedule.getScheduleUUID(), defaultCustomer.getUuid());
    assertOk(r);
    assertPlatformException(
        () -> Schedule.getOrBadRequest(defaultCustomer.getUuid(), schedule.getScheduleUUID()));
    List<ScheduleTask> scheduleTaskList = ScheduleTask.getAllTasks(schedule.getScheduleUUID());
    assertEquals(0, scheduleTaskList.size());
    assertAuditEntry(1, defaultCustomer.getUuid());
  }

  @Test
  public void testDeleteValidYb() {
    Schedule schedule =
        Schedule.create(
            defaultCustomer.getUuid(), backupTableParams, TaskType.BackupUniverse, 1000, null);
    Result r = deleteScheduleYb(schedule.getScheduleUUID(), defaultCustomer.getUuid());
    assertOk(r);
    assertPlatformException(
        () -> Schedule.getOrBadRequest(defaultCustomer.getUuid(), schedule.getScheduleUUID()));
    assertAuditEntry(1, defaultCustomer.getUuid());
  }

  @Test
  public void testDeleteInvalidCustomerUUIDYb() {
    UUID invalidCustomerUUID = UUID.randomUUID();
    Result r = deleteScheduleYb(defaultSchedule.getScheduleUUID(), invalidCustomerUUID);
    assertEquals(UNAUTHORIZED, r.status());
    String resultString = contentAsString(r);
    assertEquals(resultString, "Unable To Authenticate User");
    assertAuditEntry(0, defaultCustomer.getUuid());
  }

  @Test
  public void testDeleteInvalidScheduleUUIDYb() {
    UUID invalidScheduleUUID = UUID.randomUUID();
    Result result =
        assertPlatformException(
            () -> deleteScheduleYb(invalidScheduleUUID, defaultCustomer.getUuid()));
    assertBadRequest(result, "Invalid Schedule UUID: " + invalidScheduleUUID);
    assertAuditEntry(0, defaultCustomer.getUuid());
  }

  @Test
  public void testDeleteRunningSchedule() {
    Schedule schedule =
        Schedule.create(
            defaultCustomer.getUuid(), backupTableParams, TaskType.BackupUniverse, 1000, null);
    schedule.setRunningState(true);
    schedule.save();
    Result result =
        assertPlatformException(
            () -> deleteScheduleYb(schedule.getScheduleUUID(), defaultCustomer.getUuid()));
    assertBadRequest(result, "Cannot delete schedule as it is running.");
    assertAuditEntry(0, defaultCustomer.getUuid());
  }

  @Test
  public void testGetPagedSchedulesList() {
    // Schedule using V1 Api
    UUID tableUUID = UUID.randomUUID();
    String url =
        "/api/customers/"
            + defaultCustomer.getUuid()
            + "/universes/"
            + defaultUniverse.getUniverseUUID()
            + "/tables/"
            + tableUUID
            + "/create_backup";
    ObjectNode bodyJson = Json.newObject();
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST20");
    bodyJson.put("keyspace", "foo");
    bodyJson.put("tableName", "bar");
    bodyJson.put("actionType", "CREATE");
    bodyJson.put("storageConfigUUID", customerConfig.getConfigUUID().toString());
    bodyJson.put("cronExpression", "5 * * * *");
    bodyJson.put("isFullBackup", true);
    Result result =
        doRequestWithAuthTokenAndBody("PUT", url, defaultUser.createAuthToken(), bodyJson);
    assertOk(result);
    JsonNode resultJson = Json.parse(contentAsString(result));
    UUID scheduleUUID = UUID.fromString(resultJson.path("scheduleUUID").asText());
    Schedule schedule = Schedule.getOrBadRequest(scheduleUUID);
    assertNotNull(schedule);
    assertEquals(schedule.getCronExpression(), "5 * * * *");
    assertAuditEntry(1, defaultCustomer.getUuid());
    // Schedule using V2 Api
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    Schedule.create(
        defaultCustomer.getUuid(),
        backupTableParams,
        TaskType.CreateBackup,
        100000000L,
        "0 */2 * * *",
        TimeUnit.HOURS);
    ObjectNode bodyJson3 = Json.newObject();
    bodyJson3.put("direction", "ASC");
    bodyJson3.put("sortBy", "scheduleUUID");
    bodyJson3.put("offset", 0);
    bodyJson3.set("filter", Json.newObject().set("status", Json.newArray().add("Active")));
    result = getPagedSchedulesList(defaultCustomer.getUuid(), bodyJson3);
    assertOk(result);
    JsonNode schedulesJson = Json.parse(contentAsString(result));
    ArrayNode response = (ArrayNode) schedulesJson.get("entities");
    assertEquals(response.size(), 3);
  }

  @Test
  public void testListIncrementScheduleBackup() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    ObjectNode bodyJson2 = Json.newObject();
    Universe universe =
        ModelFactory.createUniverse(
            "Test-Universe-1",
            UUID.randomUUID(),
            defaultCustomer.getId(),
            CloudType.aws,
            null,
            null,
            true);
    backupRequestParams = new BackupRequestParams();
    backupRequestParams.incrementalBackupFrequency = 3600000L;
    backupRequestParams.incrementalBackupFrequencyTimeUnit = TimeUnit.HOURS;
    Schedule.create(
        defaultCustomer.getUuid(),
        backupRequestParams,
        TaskType.CreateBackup,
        100000000L,
        "0 */2 * * *",
        TimeUnit.HOURS);
    ObjectNode bodyJson3 = Json.newObject();
    bodyJson3.put("direction", "ASC");
    bodyJson3.put("sortBy", "scheduleUUID");
    bodyJson3.put("offset", 0);
    bodyJson3.set("filter", Json.newObject().set("status", Json.newArray().add("Active")));
    Result result = getPagedSchedulesList(defaultCustomer.getUuid(), bodyJson3);
    assertOk(result);
    JsonNode schedulesJson = Json.parse(contentAsString(result));
    ArrayNode response = (ArrayNode) schedulesJson.get("entities");
    assertEquals(2, response.size());
    System.out.println(response);
    int incrementalScheduleCount = 0;
    for (JsonNode resp : response) {
      if (resp.has("incrementalBackupFrequency")) {
        long incrementalBackupFrequency = resp.get("incrementalBackupFrequency").asLong(0);
        if (incrementalBackupFrequency > 0) {
          incrementalScheduleCount++;
        }
      }
    }
    assertEquals(1, incrementalScheduleCount);
  }

  @Test
  public void testGetPagedSchedulesListFilteredWithUniverseList() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    backupRequestParams = new BackupRequestParams();
    backupRequestParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    Schedule.create(
        defaultCustomer.getUuid(),
        backupRequestParams,
        TaskType.CreateBackup,
        100000000L,
        "0 */2 * * *",
        TimeUnit.HOURS);
    Universe universe2 = ModelFactory.createUniverse("universe-2", defaultCustomer.getId());
    backupRequestParams.setUniverseUUID(universe2.getUniverseUUID());
    Schedule.create(
        defaultCustomer.getUuid(),
        backupRequestParams,
        TaskType.CreateBackup,
        100000000L,
        "0 */2 * * *",
        TimeUnit.HOURS);
    ObjectNode bodyJson2 = Json.newObject();
    bodyJson2.put("direction", "ASC");
    bodyJson2.put("sortBy", "scheduleUUID");
    bodyJson2.put("offset", 0);
    ObjectNode filters = Json.newObject();
    filters.set("status", Json.newArray().add("Active"));
    bodyJson2.set("filter", filters);
    Result result = getPagedSchedulesList(defaultCustomer.getUuid(), bodyJson2);
    assertOk(result);
    JsonNode schedulesJson = Json.parse(contentAsString(result));
    ArrayNode response = (ArrayNode) schedulesJson.get("entities");
    assertEquals(3, response.size());
    result = getPagedSchedulesList(defaultCustomer.getUuid(), bodyJson2);
    assertOk(result);
    filters.set(
        "universeUUIDList", Json.newArray().add(defaultUniverse.getUniverseUUID().toString()));
    bodyJson2.set("filter", filters);
    result = getPagedSchedulesList(defaultCustomer.getUuid(), bodyJson2);
    schedulesJson = Json.parse(contentAsString(result));
    response = (ArrayNode) schedulesJson.get("entities");
    assertEquals(2, response.size());
    filters.set("universeUUIDList", Json.newArray().add(universe2.getUniverseUUID().toString()));
    bodyJson2.set("filter", filters);
    result = getPagedSchedulesList(defaultCustomer.getUuid(), bodyJson2);
    schedulesJson = Json.parse(contentAsString(result));
    response = (ArrayNode) schedulesJson.get("entities");
    assertEquals(1, response.size());
    filters.set("universeUUIDList", Json.newArray().add(UUID.randomUUID().toString()));
    bodyJson2.set("filter", filters);
    result = getPagedSchedulesList(defaultCustomer.getUuid(), bodyJson2);
    schedulesJson = Json.parse(contentAsString(result));
    response = (ArrayNode) schedulesJson.get("entities");
    assertEquals(0, response.size());
  }
}
