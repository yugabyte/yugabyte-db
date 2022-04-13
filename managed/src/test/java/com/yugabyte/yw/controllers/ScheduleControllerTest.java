// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.AssertHelper.assertAuditEntry;
import static com.yugabyte.yw.common.AssertHelper.assertBadRequest;
import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;
import static play.mvc.Http.Status.FORBIDDEN;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.FakeApiHelper;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.forms.EditBackupScheduleParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerConfig;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.ScheduleTask;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.Schedule.State;
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
  private Users defaultUser;
  private Schedule defaultSchedule;
  private BackupTableParams backupTableParams;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    defaultUser = ModelFactory.testUser(defaultCustomer);
    defaultUniverse = ModelFactory.createUniverse(defaultCustomer.getCustomerId());

    backupTableParams = new BackupTableParams();
    backupTableParams.universeUUID = defaultUniverse.universeUUID;
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST16");
    backupTableParams.storageConfigUUID = customerConfig.configUUID;
    defaultSchedule =
        Schedule.create(
            defaultCustomer.uuid, backupTableParams, TaskType.BackupUniverse, 1000, null);
  }

  private Result listSchedules(UUID customerUUID) {
    String authToken = defaultUser.createAuthToken();
    String method = "GET";
    String url = "/api/customers/" + customerUUID + "/schedules";

    return FakeApiHelper.doRequestWithAuthToken(method, url, authToken);
  }

  private Result deleteSchedule(UUID scheduleUUID, UUID customerUUID) {
    String authToken = defaultUser.createAuthToken();
    String method = "DELETE";
    String url = "/api/customers/" + customerUUID + "/schedules/" + scheduleUUID;

    return FakeApiHelper.doRequestWithAuthToken(method, url, authToken);
  }

  private Result deleteScheduleYb(UUID scheduleUUID, UUID customerUUID) {
    String authToken = defaultUser.createAuthToken();
    String method = "DELETE";
    String url = "/api/customers/" + customerUUID + "/schedules/" + scheduleUUID + "/delete";
    return FakeApiHelper.doRequestWithAuthToken(method, url, authToken);
  }

  private Result editSchedule(UUID scheduleUUID, UUID customerUUID, JsonNode body) {
    String authToken = defaultUser.createAuthToken();
    String method = "PUT";
    String url = "/api/customers/" + customerUUID + "/schedules/" + scheduleUUID;

    return FakeApiHelper.doRequestWithAuthTokenAndBody(method, url, authToken, body);
  }

  @Test
  public void testListWithValidCustomer() {
    Result r = listSchedules(defaultCustomer.uuid);
    assertOk(r);
    JsonNode resultJson = Json.parse(contentAsString(r));
    assertEquals(1, resultJson.size());
    assertEquals(
        resultJson.get(0).get("scheduleUUID").asText(), defaultSchedule.scheduleUUID.toString());
    assertAuditEntry(0, defaultCustomer.uuid);
  }

  @Test
  public void testListWithInvalidCustomer() {
    UUID invalidCustomerUUID = UUID.randomUUID();
    Result r = listSchedules(invalidCustomerUUID);
    assertEquals(FORBIDDEN, r.status());
    String resultString = contentAsString(r);
    assertEquals(resultString, "Unable To Authenticate User");
    assertAuditEntry(0, defaultCustomer.uuid);
  }

  @Test
  public void testDeleteValid() {
    JsonNode resultJson = Json.parse(contentAsString(listSchedules(defaultCustomer.uuid)));
    assertEquals(1, resultJson.size());
    Result r = deleteSchedule(defaultSchedule.scheduleUUID, defaultCustomer.uuid);
    assertOk(r);
    resultJson = Json.parse(contentAsString(listSchedules(defaultCustomer.uuid)));
    assertEquals(0, resultJson.size());
    assertAuditEntry(1, defaultCustomer.uuid);
  }

  @Test
  public void testDeleteInvalidCustomerUUID() {
    UUID invalidCustomerUUID = UUID.randomUUID();
    JsonNode resultJson = Json.parse(contentAsString(listSchedules(defaultCustomer.uuid)));
    assertEquals(1, resultJson.size());
    Result r = deleteSchedule(defaultSchedule.scheduleUUID, invalidCustomerUUID);
    assertEquals(FORBIDDEN, r.status());
    String resultString = contentAsString(r);
    assertEquals(resultString, "Unable To Authenticate User");
    resultJson = Json.parse(contentAsString(listSchedules(defaultCustomer.uuid)));
    assertEquals(1, resultJson.size());
    assertAuditEntry(0, defaultCustomer.uuid);
  }

  @Test
  public void testDeleteInvalidScheduleUUID() {
    UUID invalidScheduleUUID = UUID.randomUUID();
    JsonNode resultJson = Json.parse(contentAsString(listSchedules(defaultCustomer.uuid)));
    assertEquals(1, resultJson.size());
    Result result =
        assertPlatformException(() -> deleteSchedule(invalidScheduleUUID, defaultCustomer.uuid));
    assertBadRequest(result, "Invalid Schedule UUID: " + invalidScheduleUUID);
    resultJson = Json.parse(contentAsString(listSchedules(defaultCustomer.uuid)));
    assertEquals(1, resultJson.size());
    assertAuditEntry(0, defaultCustomer.uuid);
  }

  @Test
  public void testEditScheduleUpdateFrequency() {
    EditBackupScheduleParams params = new EditBackupScheduleParams();
    params.frequency = 2 * 86400L * 1000L;
    params.status = State.Active;
    params.frequencyTimeUnit = TimeUnit.DAYS;
    JsonNode requestJson = Json.toJson(params);
    Result result = editSchedule(defaultSchedule.scheduleUUID, defaultCustomer.uuid, requestJson);
    assertOk(result);
    JsonNode resultJson = Json.parse(contentAsString(listSchedules(defaultCustomer.uuid)));
    assertEquals(1, resultJson.size());
    assertEquals(
        resultJson.get(0).get("scheduleUUID").asText(), defaultSchedule.scheduleUUID.toString());
    assertTrue(resultJson.get(0).get("frequency").asLong() == params.frequency);
    assertTrue(resultJson.get(0).get("status").asText().equals(params.status.name()));
    assertAuditEntry(1, defaultCustomer.uuid);
  }

  @Test
  public void testEditScheduleUpdateFrequencyWithoutTimeUnit() {
    EditBackupScheduleParams params = new EditBackupScheduleParams();
    params.frequency = 2 * 86400L * 1000L;
    params.status = State.Active;
    JsonNode requestJson = Json.toJson(params);
    Result result =
        assertPlatformException(
            () -> editSchedule(defaultSchedule.scheduleUUID, defaultCustomer.uuid, requestJson));
    assertBadRequest(result, "Please provide time unit for frequency");
  }

  @Test
  public void testEditScheduleUpdateCronExpression() {
    EditBackupScheduleParams params = new EditBackupScheduleParams();
    params.cronExpression = "0 12 * * *";
    params.status = State.Active;
    JsonNode requestJson = Json.toJson(params);
    Result result = editSchedule(defaultSchedule.scheduleUUID, defaultCustomer.uuid, requestJson);
    assertOk(result);
    JsonNode resultJson = Json.parse(contentAsString(listSchedules(defaultCustomer.uuid)));
    assertEquals(1, resultJson.size());
    assertEquals(
        resultJson.get(0).get("scheduleUUID").asText(), defaultSchedule.scheduleUUID.toString());
    assertEquals(resultJson.get(0).get("cronExpression").asText(), params.cronExpression);
    assertTrue(resultJson.get(0).get("status").asText().equals(params.status.name()));
    assertAuditEntry(1, defaultCustomer.uuid);
  }

  @Test
  public void testEditScheduleUpdateCronExpressionWithStateAsStopped() {
    EditBackupScheduleParams params = new EditBackupScheduleParams();
    params.status = State.Stopped;
    params.frequency = 2 * 86400L * 1000L;
    JsonNode requestJson = Json.toJson(params);
    Result result = editSchedule(defaultSchedule.scheduleUUID, defaultCustomer.uuid, requestJson);
    assertOk(result);
    JsonNode resultJson = Json.parse(contentAsString(listSchedules(defaultCustomer.uuid)));
    assertEquals(0, resultJson.size());
    assertAuditEntry(1, defaultCustomer.uuid);
  }

  @Test
  public void testEditScheduleUpdateCronExpressionToThirtyMins() {
    EditBackupScheduleParams params = new EditBackupScheduleParams();
    params.cronExpression = "15,45 * * * *";
    params.status = State.Active;
    JsonNode requestJson = Json.toJson(params);
    Result result =
        assertPlatformException(
            () -> editSchedule(defaultSchedule.scheduleUUID, defaultCustomer.uuid, requestJson));
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
            () -> editSchedule(defaultSchedule.scheduleUUID, defaultCustomer.uuid, requestJson));
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
            () -> editSchedule(defaultSchedule.scheduleUUID, defaultCustomer.uuid, requestJson));
    assertBadRequest(result, "Both schedule frequency and cron expression cannot be provided");
  }

  @Test
  public void testEditScheduleUpdateCronExpressionWithNoParamsProvided() {
    EditBackupScheduleParams params = new EditBackupScheduleParams();
    params.status = State.Active;
    JsonNode requestJson = Json.toJson(params);
    Result result =
        assertPlatformException(
            () -> editSchedule(defaultSchedule.scheduleUUID, defaultCustomer.uuid, requestJson));
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
            () -> editSchedule(defaultSchedule.scheduleUUID, defaultCustomer.uuid, requestJson));
    assertBadRequest(
        result, "State paused is an internal state and cannot be specified by the user");
  }

  @Test
  public void testDeleteValidScheduleHavingTaskYb() {
    Schedule schedule =
        Schedule.create(
            defaultCustomer.uuid, backupTableParams, TaskType.BackupUniverse, 1000, null);
    UUID randomTaskUUID = UUID.randomUUID();
    ScheduleTask.create(randomTaskUUID, schedule.scheduleUUID);
    Result r = deleteScheduleYb(schedule.scheduleUUID, defaultCustomer.uuid);
    assertOk(r);
    assertPlatformException(
        () -> Schedule.getOrBadRequest(defaultCustomer.uuid, schedule.scheduleUUID));
    List<ScheduleTask> scheduleTaskList = ScheduleTask.getAllTasks(schedule.scheduleUUID);
    assertEquals(0, scheduleTaskList.size());
    assertAuditEntry(1, defaultCustomer.uuid);
  }

  @Test
  public void testDeleteValidYb() {
    Schedule schedule =
        Schedule.create(
            defaultCustomer.uuid, backupTableParams, TaskType.BackupUniverse, 1000, null);
    Result r = deleteScheduleYb(schedule.scheduleUUID, defaultCustomer.uuid);
    assertOk(r);
    assertPlatformException(
        () -> Schedule.getOrBadRequest(defaultCustomer.uuid, schedule.scheduleUUID));
    assertAuditEntry(1, defaultCustomer.uuid);
  }

  @Test
  public void testDeleteInvalidCustomerUUIDYb() {
    UUID invalidCustomerUUID = UUID.randomUUID();
    Result r = deleteScheduleYb(defaultSchedule.scheduleUUID, invalidCustomerUUID);
    assertEquals(FORBIDDEN, r.status());
    String resultString = contentAsString(r);
    assertEquals(resultString, "Unable To Authenticate User");
    assertAuditEntry(0, defaultCustomer.uuid);
  }

  @Test
  public void testDeleteInvalidScheduleUUIDYb() {
    UUID invalidScheduleUUID = UUID.randomUUID();
    Result result =
        assertPlatformException(() -> deleteScheduleYb(invalidScheduleUUID, defaultCustomer.uuid));
    assertBadRequest(result, "Invalid Schedule UUID: " + invalidScheduleUUID);
    assertAuditEntry(0, defaultCustomer.uuid);
  }

  @Test
  public void testDeleteRunningSchedule() {
    Schedule schedule =
        Schedule.create(
            defaultCustomer.uuid, backupTableParams, TaskType.BackupUniverse, 1000, null);
    schedule.setRunningState(true);
    Result result =
        assertPlatformException(
            () -> deleteScheduleYb(schedule.scheduleUUID, defaultCustomer.uuid));
    assertBadRequest(result, "Cannot delete schedule as it is running.");
    assertAuditEntry(0, defaultCustomer.uuid);
  }
}
