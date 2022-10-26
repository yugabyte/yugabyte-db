// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.AssertHelper.assertAuditEntry;
import static com.yugabyte.yw.common.AssertHelper.assertBadRequest;
import static com.yugabyte.yw.common.AssertHelper.assertErrorNodeValue;
import static com.yugabyte.yw.common.AssertHelper.assertInternalServerError;
import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static com.yugabyte.yw.common.AssertHelper.assertValue;
import static com.yugabyte.yw.common.AssertHelper.assertValues;
import static com.yugabyte.yw.models.CustomerTask.TaskType.Restore;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.doNothing;
import static org.mockito.Mockito.doThrow;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.FORBIDDEN;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.FakeApiHelper;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.forms.RestoreBackupParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Backup.BackupCategory;
import com.yugabyte.yw.models.Backup.BackupState;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.TaskInfo.State;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.configs.CustomerConfig.ConfigState;
import com.yugabyte.yw.models.helpers.TaskType;
import java.io.IOException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.Callable;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import junitparams.naming.TestCaseName;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import play.libs.Json;
import play.mvc.Result;

@RunWith(JUnitParamsRunner.class)
public class BackupsControllerTest extends FakeDBApplication {

  private Universe defaultUniverse;
  private Users defaultUser;
  private Customer defaultCustomer;
  private Backup defaultBackup;
  private CustomerConfig customerConfig;
  private BackupTableParams backupTableParams;
  private UUID taskUUID;
  private TaskInfo taskInfo;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    defaultUser = ModelFactory.testUser(defaultCustomer);
    defaultUniverse = ModelFactory.createUniverse(defaultCustomer.getCustomerId());
    taskUUID = UUID.randomUUID();
    backupTableParams = new BackupTableParams();
    backupTableParams.universeUUID = defaultUniverse.universeUUID;
    customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST105");
    backupTableParams.storageConfigUUID = customerConfig.configUUID;
    backupTableParams.customerUuid = defaultCustomer.uuid;
    defaultBackup = Backup.create(defaultCustomer.uuid, backupTableParams);
    defaultBackup.setTaskUUID(taskUUID);
  }

  private JsonNode listBackups(UUID universeUUID) {
    String authToken = defaultUser.createAuthToken();
    String method = "GET";
    String url =
        "/api/customers/" + defaultCustomer.uuid + "/universes/" + universeUUID + "/backups";

    Result r = FakeApiHelper.doRequestWithAuthToken(method, url, authToken);
    assertOk(r);
    return Json.parse(contentAsString(r));
  }

  @Test
  public void testListWithValidUniverse() {
    JsonNode resultJson = listBackups(defaultUniverse.universeUUID);
    assertEquals(1, resultJson.size());
    assertValues(resultJson, "backupUUID", ImmutableList.of(defaultBackup.backupUUID.toString()));
    assertAuditEntry(0, defaultCustomer.uuid);
  }

  @Test
  public void testListWithInvalidUniverse() {
    JsonNode resultJson = listBackups(UUID.randomUUID());
    assertEquals(0, resultJson.size());
    assertAuditEntry(0, defaultCustomer.uuid);
  }

  @Test
  public void testListWithHiddenStorage() {
    JsonNode features =
        Json.parse(
            "{\"universes\": { \"details\": { \"backups\": { \"storageLocation\": \"hidden\"}}}}");
    defaultCustomer.upsertFeatures(features);
    assertEquals(features, defaultCustomer.getFeatures());

    BackupTableParams btp = new BackupTableParams();
    btp.universeUUID = defaultUniverse.universeUUID;
    btp.storageConfigUUID = UUID.randomUUID();
    Backup backup = Backup.create(defaultCustomer.uuid, btp);
    backup.setTaskUUID(taskUUID);
    // Patching manually. The broken backups left from previous releases, currently we can't create
    // such backups through API.
    btp.storageLocation = null;
    backup.setBackupInfo(btp);
    backup.save();

    JsonNode resultJson = listBackups(defaultUniverse.universeUUID);
    assertEquals(2, resultJson.size());
    assertValues(
        resultJson,
        "backupUUID",
        ImmutableList.of(defaultBackup.backupUUID.toString(), backup.backupUUID.toString()));

    // Only one storageLocation should be in values as null values are filtered.
    assertValues(resultJson, "storageLocation", ImmutableList.of("**********"));
  }

  private JsonNode fetchBackupsbyTaskId(UUID universeUUID, UUID taskUUID) {
    String authToken = defaultUser.createAuthToken();
    String method = "GET";
    String url =
        "/api/customers/"
            + defaultCustomer.uuid
            + "/universes/"
            + universeUUID
            + "/backups/tasks/"
            + taskUUID;

    Result r = FakeApiHelper.doRequestWithAuthToken(method, url, authToken);
    assertOk(r);
    return Json.parse(contentAsString(r));
  }

  @Test
  public void testFetchBackupsByTaskUUIDWithSingleEntry() {
    JsonNode resultJson = fetchBackupsbyTaskId(defaultUniverse.universeUUID, taskUUID);
    assertEquals(1, resultJson.size());
    assertValues(resultJson, "backupUUID", ImmutableList.of(defaultBackup.backupUUID.toString()));
    assertAuditEntry(0, defaultCustomer.uuid);
  }

  @Test
  public void testCreateBackup() {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST21");
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("universeUUID", defaultUniverse.universeUUID.toString());
    bodyJson.put("backupType", "PGSQL_TABLE_TYPE");
    bodyJson.put("storageConfigUUID", customerConfig.configUUID.toString());
    Result r = createBackupYb(bodyJson, null);
    JsonNode resultJson = Json.parse(contentAsString(r));
    assertValue(resultJson, "taskUUID", fakeTaskUUID.toString());
    assertEquals(OK, r.status());
    verify(mockCommissioner, times(1)).submit(any(), any());
  }

  @Test
  public void testCreateScheduledBackupValidCron() {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST22");
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("universeUUID", defaultUniverse.universeUUID.toString());
    bodyJson.put("storageConfigUUID", customerConfig.configUUID.toString());
    bodyJson.put("cronExpression", "0 */2 * * *");
    bodyJson.put("scheduleName", "schedule-1");
    bodyJson.put("backupType", "PGSQL_TABLE_TYPE");
    Result r = createBackupSchedule(bodyJson, null);
    assertEquals(OK, r.status());
  }

  @Test
  public void testCreateBackupScheduleWithTimeUnit() {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST25");
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("universeUUID", defaultUniverse.universeUUID.toString());
    bodyJson.put("storageConfigUUID", customerConfig.configUUID.toString());
    bodyJson.put("schedulingFrequency", 10000000L);
    bodyJson.put("scheduleName", "schedule-1");
    bodyJson.put("frequencyTimeUnit", "HOURS");
    bodyJson.put("backupType", "PGSQL_TABLE_TYPE");
    Result r = createBackupSchedule(bodyJson, null);
    assertEquals(OK, r.status());
  }

  @Test
  public void testCreateScheduleBackupWithoutName() {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST22");
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("universeUUID", defaultUniverse.universeUUID.toString());
    bodyJson.put("storageConfigUUID", customerConfig.configUUID.toString());
    bodyJson.put("cronExpression", "0 */2 * * *");
    bodyJson.put("backupType", "PGSQL_TABLE_TYPE");
    Result result = assertPlatformException(() -> createBackupSchedule(bodyJson, null));
    assertBadRequest(result, "Provide a name for the schedule");
  }

  @Test
  public void testCreateScheduleBackupWithoutTimeUnit() {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST25");
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("universeUUID", defaultUniverse.universeUUID.toString());
    bodyJson.put("storageConfigUUID", customerConfig.configUUID.toString());
    bodyJson.put("schedulingFrequency", 10000000L);
    bodyJson.put("backupType", "PGSQL_TABLE_TYPE");
    bodyJson.put("scheduleName", "schedule-1");
    Result r = assertPlatformException(() -> createBackupSchedule(bodyJson, null));
    assertBadRequest(r, "Please provide time unit for scheduler frequency");
  }

  @Test
  public void testCreateScheduleBackupWithDuplicateName() {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST22");
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("universeUUID", defaultUniverse.universeUUID.toString());
    bodyJson.put("storageConfigUUID", customerConfig.configUUID.toString());
    bodyJson.put("cronExpression", "0 */2 * * *");
    bodyJson.put("backupType", "PGSQL_TABLE_TYPE");
    bodyJson.put("scheduleName", "schedule-1");
    Result r = createBackupSchedule(bodyJson, null);
    assertEquals(OK, r.status());
    Result result = assertPlatformException(() -> createBackupSchedule(bodyJson, null));
    assertBadRequest(result, "Schedule with name schedule-1 already exist");
  }

  @Test
  public void testCreateScheduledBackupInvalid() {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST24");
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("universeUUID", defaultUniverse.universeUUID.toString());
    bodyJson.put("storageConfigUUID", customerConfig.configUUID.toString());
    bodyJson.put("scheduleName", "schedule-1");
    bodyJson.put("backupType", "PGSQL_TABLE_TYPE");
    Result r = assertPlatformException(() -> createBackupSchedule(bodyJson, null));
    JsonNode resultJson = Json.parse(contentAsString(r));
    assertValue(resultJson, "error", "Provide Cron Expression or Scheduling frequency");
    assertEquals(BAD_REQUEST, r.status());
    verify(mockCommissioner, times(0)).submit(any(), any());
  }

  @Test
  public void testCreateIncrementalScheduleBackupSuccess() {
    ObjectNode bodyJson = Json.newObject();
    Universe universe =
        ModelFactory.createUniverse(
            "Test-Universe-1",
            UUID.randomUUID(),
            defaultCustomer.getCustomerId(),
            CloudType.aws,
            null,
            null,
            true);
    bodyJson.put("universeUUID", universe.universeUUID.toString());
    bodyJson.put("storageConfigUUID", customerConfig.configUUID.toString());
    bodyJson.put("schedulingFrequency", 1000000000L);
    bodyJson.put("scheduleName", "schedule-1");
    bodyJson.put("frequencyTimeUnit", "HOURS");
    bodyJson.put("incrementalBackupFrequency", 10000000L);
    bodyJson.put("incrementalBackupFrequencyTimeUnit", "HOURS");
    bodyJson.put("backupType", "PGSQL_TABLE_TYPE");
    Result r = createBackupSchedule(bodyJson, null);
    assertEquals(OK, r.status());
  }

  @Test
  public void testCreateIncrementalScheduleBackupWithOutFrequencyTimeUnit() {
    ObjectNode bodyJson = Json.newObject();
    Universe universe =
        ModelFactory.createUniverse(
            "Test-Universe-1",
            UUID.randomUUID(),
            defaultCustomer.getCustomerId(),
            CloudType.aws,
            null,
            null,
            true);
    bodyJson.put("universeUUID", universe.universeUUID.toString());
    bodyJson.put("storageConfigUUID", customerConfig.configUUID.toString());
    bodyJson.put("schedulingFrequency", 1000000000L);
    bodyJson.put("scheduleName", "schedule-1");
    bodyJson.put("frequencyTimeUnit", "HOURS");
    bodyJson.put("incrementalBackupFrequency", 10000000L);
    bodyJson.put("backupType", "PGSQL_TABLE_TYPE");
    Result r = assertPlatformException(() -> createBackupSchedule(bodyJson, null));
    JsonNode resultJson = Json.parse(contentAsString(r));
    assertValue(resultJson, "error", "Please provide time unit for incremental backup frequency.");
    assertEquals(BAD_REQUEST, r.status());
  }

  @Test
  public void testCreateIncrementalScheduleBackupOnNonYbcUniverse() {
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("universeUUID", defaultUniverse.universeUUID.toString());
    bodyJson.put("storageConfigUUID", customerConfig.configUUID.toString());
    bodyJson.put("schedulingFrequency", 1000000000L);
    bodyJson.put("scheduleName", "schedule-1");
    bodyJson.put("frequencyTimeUnit", "HOURS");
    bodyJson.put("incrementalBackupFrequency", 10000000L);
    bodyJson.put("incrementalBackupFrequencyTimeUnit", "HOURS");
    bodyJson.put("backupType", "PGSQL_TABLE_TYPE");
    Result r = assertPlatformException(() -> createBackupSchedule(bodyJson, null));
    JsonNode resultJson = Json.parse(contentAsString(r));
    assertValue(
        resultJson, "error", "Cannot create incremental backup schedules on non-ybc universes.");
    assertEquals(BAD_REQUEST, r.status());
  }

  @Test
  public void testCreateIncrementalScheduleBackupWithInvalidIncrementalFrequency() {
    ObjectNode bodyJson = Json.newObject();
    Universe universe =
        ModelFactory.createUniverse(
            "Test-Universe-1",
            UUID.randomUUID(),
            defaultCustomer.getCustomerId(),
            CloudType.aws,
            null,
            null,
            true);
    bodyJson.put("universeUUID", universe.universeUUID.toString());
    bodyJson.put("storageConfigUUID", customerConfig.configUUID.toString());
    bodyJson.put("cronExpression", "0 */2 * * *");
    bodyJson.put("scheduleName", "schedule-1");
    bodyJson.put("incrementalBackupFrequency", 10000000000L);
    bodyJson.put("incrementalBackupFrequencyTimeUnit", "HOURS");
    bodyJson.put("backupType", "PGSQL_TABLE_TYPE");
    Result r = assertPlatformException(() -> createBackupSchedule(bodyJson, null));
    JsonNode resultJson = Json.parse(contentAsString(r));
    assertValue(
        resultJson,
        "error",
        "Incremental backup frequency should be lower than full backup frequency.");
    assertEquals(BAD_REQUEST, r.status());
    bodyJson.put("schedulingFrequency", 10000000000L);
    bodyJson.put("frequencyTimeUnit", "HOURS");
    bodyJson.remove("cronExpression");
    System.out.println("***N Is: " + bodyJson);
    r = assertPlatformException(() -> createBackupSchedule(bodyJson, null));
    resultJson = Json.parse(contentAsString(r));
    assertValue(
        resultJson,
        "error",
        "Incremental backup frequency should be lower than full backup frequency.");
    assertEquals(BAD_REQUEST, r.status());
    bodyJson.put("incrementalBackupFrequency", 100000L);
    r = assertPlatformException(() -> createBackupSchedule(bodyJson, null));
    resultJson = Json.parse(contentAsString(r));
    assertValue(resultJson, "error", "Minimum schedule duration is 1 hour");
    assertEquals(BAD_REQUEST, r.status());
    bodyJson.put("incrementalBackupFrequency", 1000000000L);
    r = createBackupSchedule(bodyJson, null);
    assertOk(r);
  }

  @Test
  public void testCreateIncrementalScheduleBackupWithBaseBackup() {
    ObjectNode bodyJson = Json.newObject();
    Universe universe =
        ModelFactory.createUniverse(
            "Test-Universe-1",
            UUID.randomUUID(),
            defaultCustomer.getCustomerId(),
            CloudType.aws,
            null,
            null,
            true);
    bodyJson.put("universeUUID", universe.universeUUID.toString());
    bodyJson.put("storageConfigUUID", customerConfig.configUUID.toString());
    bodyJson.put("schedulingFrequency", 1000000000L);
    bodyJson.put("scheduleName", "schedule-1");
    bodyJson.put("frequencyTimeUnit", "HOURS");
    bodyJson.put("incrementalBackupFrequency", 10000000L);
    bodyJson.put("incrementalBackupFrequencyTimeUnit", "HOURS");
    bodyJson.put("baseBackupUUID", UUID.randomUUID().toString());
    bodyJson.put("backupType", "PGSQL_TABLE_TYPE");
    Result r = assertPlatformException(() -> createBackupSchedule(bodyJson, null));
    JsonNode resultJson = Json.parse(contentAsString(r));
    assertValue(resultJson, "error", "Cannot assign base backup while creating backup schedules.");
    assertEquals(BAD_REQUEST, r.status());
  }

  @Test
  public void testCreateBackupValidationFailed() {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST25");
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    doThrow(new PlatformServiceException(BAD_REQUEST, "error"))
        .when(mockBackupUtil)
        .validateTables(any(), any(), any(), any());
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("universeUUID", defaultUniverse.universeUUID.toString());
    bodyJson.put("storageConfigUUID", customerConfig.configUUID.toString());
    bodyJson.put("backupType", "PGSQL_TABLE_TYPE");
    Result r = assertPlatformException(() -> createBackupYb(bodyJson, null));
    JsonNode resultJson = Json.parse(contentAsString(r));
    assertValue(resultJson, "error", "error");
    assertEquals(BAD_REQUEST, r.status());
    verify(mockCommissioner, times(0)).submit(any(), any());
  }

  @Test
  public void testCreateBackupWithoutTimeUnit() {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST26");
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("universeUUID", defaultUniverse.universeUUID.toString());
    bodyJson.put("storageConfigUUID", customerConfig.configUUID.toString());
    bodyJson.put("timeBeforeDelete", 100000L);
    bodyJson.put("backupType", "PGSQL_TABLE_TYPE");
    Result r = assertPlatformException(() -> createBackupYb(bodyJson, null));
    assertBadRequest(r, "Please provide time unit for backup expiry");
    verify(mockCommissioner, times(0)).submit(any(), any());
  }

  @Test
  public void testFetchBackupsByTaskUUIDWithMultipleEntries() {
    Backup backup2 = Backup.create(defaultCustomer.uuid, backupTableParams);
    backup2.setTaskUUID(taskUUID);

    JsonNode resultJson = fetchBackupsbyTaskId(defaultUniverse.universeUUID, taskUUID);
    assertEquals(2, resultJson.size());
    assertValues(
        resultJson,
        "backupUUID",
        ImmutableList.of(defaultBackup.backupUUID.toString(), backup2.backupUUID.toString()));
    assertAuditEntry(0, defaultCustomer.uuid);
  }

  @Test
  public void testFetchBackupsByTaskUUIDWithDifferentTaskEntries() {
    Backup backup2 = Backup.create(defaultCustomer.uuid, backupTableParams);
    backup2.setTaskUUID(taskUUID);
    Backup backup3 = Backup.create(defaultCustomer.uuid, backupTableParams);
    backup3.setTaskUUID(UUID.randomUUID());

    JsonNode resultJson = fetchBackupsbyTaskId(defaultUniverse.universeUUID, taskUUID);
    assertEquals(2, resultJson.size());
    assertValues(
        resultJson,
        "backupUUID",
        ImmutableList.of(defaultBackup.backupUUID.toString(), backup2.backupUUID.toString()));
    assertAuditEntry(0, defaultCustomer.uuid);
  }

  private Result restoreBackup(UUID universeUUID, JsonNode bodyJson, Users user) {
    String authToken = defaultUser.createAuthToken();
    if (user != null) {
      authToken = user.createAuthToken();
    }
    String method = "POST";
    String url =
        "/api/customers/"
            + defaultCustomer.uuid
            + "/universes/"
            + universeUUID
            + "/backups/restore";
    return FakeApiHelper.doRequestWithAuthTokenAndBody(method, url, authToken, bodyJson);
  }

  private Result restoreBackupYb(JsonNode bodyJson, Users user) {
    String authToken = defaultUser.createAuthToken();
    if (user != null) {
      authToken = user.createAuthToken();
    }
    String method = "POST";
    String url = "/api/customers/" + defaultCustomer.uuid + "/restore";
    return FakeApiHelper.doRequestWithAuthTokenAndBody(method, url, authToken, bodyJson);
  }

  private Result deleteBackup(ObjectNode bodyJson, Users user) {
    String authToken = user == null ? defaultUser.createAuthToken() : user.createAuthToken();
    String method = "DELETE";
    String url = "/api/customers/" + defaultCustomer.uuid + "/backups";
    return FakeApiHelper.doRequestWithAuthTokenAndBody(method, url, authToken, bodyJson);
  }

  private Result deleteBackupYb(ObjectNode bodyJson, Users user) {
    String authToken = user == null ? defaultUser.createAuthToken() : user.createAuthToken();
    String method = "POST";
    String url = "/api/customers/" + defaultCustomer.uuid + "/backups/delete";
    return FakeApiHelper.doRequestWithAuthTokenAndBody(method, url, authToken, bodyJson);
  }

  private Result createBackupYb(ObjectNode bodyJson, Users user) {
    String authToken = user == null ? defaultUser.createAuthToken() : user.createAuthToken();
    String method = "POST";
    String url = "/api/customers/" + defaultCustomer.uuid + "/backups";
    return FakeApiHelper.doRequestWithAuthTokenAndBody(method, url, authToken, bodyJson);
  }

  private Result createBackupSchedule(ObjectNode bodyJson, Users user) {
    String authToken = user == null ? defaultUser.createAuthToken() : user.createAuthToken();
    String method = "POST";
    String url = "/api/customers/" + defaultCustomer.uuid + "/create_backup_schedule";
    return FakeApiHelper.doRequestWithAuthTokenAndBody(method, url, authToken, bodyJson);
  }

  private Result stopBackup(Users user, UUID backupUUID) {
    String authToken = user == null ? defaultUser.createAuthToken() : user.createAuthToken();
    String method = "POST";
    String url = "/api/customers/" + defaultCustomer.uuid + "/backups/" + backupUUID + "/stop";
    return FakeApiHelper.doRequestWithAuthToken(method, url, authToken);
  }

  private Result editBackup(Users user, ObjectNode bodyJson, UUID backupUUID) {
    String authToken = user == null ? defaultUser.createAuthToken() : user.createAuthToken();
    String method = "PUT";
    String url = "/api/customers/" + defaultCustomer.uuid + "/backups/" + backupUUID;
    return FakeApiHelper.doRequestWithAuthTokenAndBody(method, url, authToken, bodyJson);
  }

  @Test
  public void testRestoreBackupWithInvalidUniverseUUID() {
    UUID universeUUID = UUID.randomUUID();
    JsonNode bodyJson = Json.newObject();

    Result result = assertPlatformException(() -> restoreBackup(universeUUID, bodyJson, null));
    assertEquals(BAD_REQUEST, result.status());
    JsonNode resultJson = Json.parse(contentAsString(result));
    assertValue(resultJson, "error", "Cannot find universe " + universeUUID);
    assertAuditEntry(0, defaultCustomer.uuid);
  }

  @Test
  public void testRestoreBackupWithInvalidParams() {
    BackupTableParams bp = new BackupTableParams();
    bp.storageConfigUUID = UUID.randomUUID();
    bp.universeUUID = UUID.randomUUID();
    Backup.create(defaultCustomer.uuid, bp);
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("actionType", "RESTORE");
    Result result =
        assertPlatformException(() -> restoreBackup(defaultUniverse.universeUUID, bodyJson, null));
    assertEquals(BAD_REQUEST, result.status());
    JsonNode resultJson = Json.parse(contentAsString(result));
    assertErrorNodeValue(resultJson, "storageConfigUUID", "This field is required");
    assertAuditEntry(0, defaultCustomer.uuid);
  }

  @Test
  public void testRestoreBackupWithoutStorageLocation() {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST2");
    BackupTableParams bp = new BackupTableParams();
    bp.storageConfigUUID = customerConfig.configUUID;
    bp.universeUUID = UUID.randomUUID();
    Backup.create(defaultCustomer.uuid, bp);
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("keyspace", "mock_ks");
    bodyJson.put("tableName", "mock_table");
    bodyJson.put("actionType", "RESTORE");
    bodyJson.put("storageConfigUUID", bp.storageConfigUUID.toString());
    Result result =
        assertPlatformException(() -> restoreBackup(defaultUniverse.universeUUID, bodyJson, null));
    assertEquals(BAD_REQUEST, result.status());
    JsonNode resultJson = Json.parse(contentAsString(result));
    assertValue(resultJson, "error", "Storage Location is required");
    assertAuditEntry(0, defaultCustomer.uuid);
  }

  @Test
  public void testRestoreBackupWithInvalidStorageUUID() {
    BackupTableParams bp = new BackupTableParams();
    bp.storageConfigUUID = UUID.randomUUID();
    bp.universeUUID = UUID.randomUUID();
    Backup b = Backup.create(defaultCustomer.uuid, bp);
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("keyspace", "mock_ks");
    bodyJson.put("tableName", "mock_table");
    bodyJson.put("actionType", "RESTORE");
    bodyJson.put("storageConfigUUID", bp.storageConfigUUID.toString());
    bodyJson.put("storageLocation", b.getBackupInfo().storageLocation);
    Result result =
        assertPlatformException(() -> restoreBackup(defaultUniverse.universeUUID, bodyJson, null));
    assertEquals(BAD_REQUEST, result.status());
    JsonNode resultJson = Json.parse(contentAsString(result));
    assertValue(resultJson, "error", "Invalid StorageConfig UUID: " + bp.storageConfigUUID);
    assertAuditEntry(0, defaultCustomer.uuid);
  }

  @Test
  public void testRestoreBackupWithReadOnlyUser() {
    Users user = ModelFactory.testUser(defaultCustomer, "tc@test.com", Users.Role.ReadOnly);
    BackupTableParams bp = new BackupTableParams();
    bp.storageConfigUUID = UUID.randomUUID();
    bp.universeUUID = UUID.randomUUID();
    Backup b = Backup.create(defaultCustomer.uuid, bp);
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("keyspace", "mock_ks");
    bodyJson.put("tableName", "mock_table");
    bodyJson.put("actionType", "RESTORE");
    bodyJson.put("storageConfigUUID", bp.storageConfigUUID.toString());
    bodyJson.put("storageLocation", b.getBackupInfo().storageLocation);
    Result result = restoreBackup(defaultUniverse.universeUUID, bodyJson, user);
    assertEquals(FORBIDDEN, result.status());
    assertEquals("User doesn't have access", contentAsString(result));
    assertAuditEntry(0, defaultCustomer.uuid);
  }

  @Test
  public void testRestoreBackupWithValidParams() {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST3");
    BackupTableParams bp = new BackupTableParams();
    bp.storageConfigUUID = customerConfig.configUUID;
    bp.universeUUID = defaultUniverse.universeUUID;
    Backup b = Backup.create(defaultCustomer.uuid, bp);
    ObjectNode bodyJson = Json.newObject();

    long maxReqSizeInBytes =
        app.config().getMemorySize("play.http.parser.maxMemoryBuffer").toBytes();

    // minus 1000 so as to leave some room for other fields and headers etc.
    int keyspaceSz = (int) (maxReqSizeInBytes - 1000);

    // Intentionally use large keyspace field approaching (but not exceeding) 500k
    // (which is
    // now a default for play.http.parser.maxMemoryBuffer)
    String largeKeyspace = new String(new char[keyspaceSz]).replace("\0", "#");
    bodyJson.put("keyspace", largeKeyspace);
    bodyJson.put("actionType", "RESTORE");
    bodyJson.put("storageConfigUUID", bp.storageConfigUUID.toString());
    bodyJson.put("storageLocation", "s3://foo/bar");

    ArgumentCaptor<TaskType> taskType = ArgumentCaptor.forClass(TaskType.class);
    ArgumentCaptor<BackupTableParams> taskParams = ArgumentCaptor.forClass(BackupTableParams.class);

    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    Result result = restoreBackup(defaultUniverse.universeUUID, bodyJson, null);
    verify(mockCommissioner, times(1)).submit(taskType.capture(), taskParams.capture());
    assertEquals(TaskType.BackupUniverse, taskType.getValue());
    assertOk(result);
    JsonNode resultJson = Json.parse(contentAsString(result));
    assertValue(resultJson, "taskUUID", fakeTaskUUID.toString());
    CustomerTask ct = CustomerTask.findByTaskUUID(fakeTaskUUID);
    assertNotNull(ct);
    assertEquals(Restore, ct.getType());
    assertAuditEntry(1, defaultCustomer.uuid);
  }

  @Test
  public void testYbcRestoreCategory() {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST12");
    BackupTableParams bp = new BackupTableParams();
    bp.storageConfigUUID = customerConfig.configUUID;
    bp.universeUUID = defaultUniverse.universeUUID;
    Backup b = Backup.create(defaultCustomer.uuid, bp);
    ObjectNode bodyJson = Json.newObject();
    JsonNode storageInfoParam =
        Json.parse(
            "{\"backupType\": \"PGSQL_TABLE_TYPE\","
                + "\"keyspace\": \"bar\","
                + "\"storageLocation\": \"s3://foo-1/"
                + "univ-"
                + defaultUniverse.universeUUID.toString()
                + "/ybc_backup/bar\"}");
    ArrayNode storageArrayNode = Json.newArray();
    storageArrayNode.add(storageInfoParam);
    bodyJson.put("backupStorageInfoList", storageArrayNode);
    bodyJson.put("storageConfigUUID", bp.storageConfigUUID.toString());
    bodyJson.put("universeUUID", bp.universeUUID.toString());
    bodyJson.put("customerUUID", defaultCustomer.uuid.toString());

    ArgumentCaptor<TaskType> taskType = ArgumentCaptor.forClass(TaskType.class);
    ArgumentCaptor<RestoreBackupParams> taskParams =
        ArgumentCaptor.forClass(RestoreBackupParams.class);

    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockBackupUtil.isYbcBackup(anyString())).thenCallRealMethod();
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    Result result = restoreBackupYb(bodyJson, null);
    verify(mockCommissioner, times(1)).submit(taskType.capture(), taskParams.capture());
    assertEquals(TaskType.RestoreBackup, taskType.getValue());
    // Assert category is set to YB_CONTROLLER for YB-Controller backups.
    assertEquals(BackupCategory.YB_CONTROLLER, taskParams.getValue().category);
    assertOk(result);
    JsonNode resultJson = Json.parse(contentAsString(result));
    assertValue(resultJson, "taskUUID", fakeTaskUUID.toString());
    CustomerTask ct = CustomerTask.findByTaskUUID(fakeTaskUUID);
    assertNotNull(ct);
    assertEquals(Restore, ct.getType());
    assertAuditEntry(1, defaultCustomer.uuid);
  }

  @Test
  public void testYbcBackupCategoryNonYbc() {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST15");
    BackupTableParams bp = new BackupTableParams();
    bp.storageConfigUUID = customerConfig.configUUID;
    bp.universeUUID = defaultUniverse.universeUUID;
    Backup b = Backup.create(defaultCustomer.uuid, bp);
    ObjectNode bodyJson = Json.newObject();
    JsonNode storageInfoParam =
        Json.parse(
            "{\"backupType\": \"PGSQL_TABLE_TYPE\","
                + "\"keyspace\": \"bar\","
                + "\"storageLocation\": \"s3://foo/"
                + "univ-"
                + defaultUniverse.universeUUID.toString()
                + "/backup/bar\"}");
    ArrayNode storageArrayNode = Json.newArray();
    storageArrayNode.add(storageInfoParam);
    bodyJson.put("backupStorageInfoList", storageArrayNode);
    bodyJson.put("storageConfigUUID", bp.storageConfigUUID.toString());
    bodyJson.put("universeUUID", bp.universeUUID.toString());
    bodyJson.put("customerUUID", defaultCustomer.uuid.toString());

    ArgumentCaptor<TaskType> taskType = ArgumentCaptor.forClass(TaskType.class);
    ArgumentCaptor<RestoreBackupParams> taskParams =
        ArgumentCaptor.forClass(RestoreBackupParams.class);

    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockBackupUtil.isYbcBackup(anyString())).thenCallRealMethod();
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    Result result = restoreBackupYb(bodyJson, null);
    verify(mockCommissioner, times(1)).submit(taskType.capture(), taskParams.capture());
    assertEquals(TaskType.RestoreBackup, taskType.getValue());
    // Assert category is set to YB_BACKUP_SCRIPT for Script backups.
    assertEquals(BackupCategory.YB_BACKUP_SCRIPT, taskParams.getValue().category);
    assertOk(result);
    JsonNode resultJson = Json.parse(contentAsString(result));
    assertValue(resultJson, "taskUUID", fakeTaskUUID.toString());
    CustomerTask ct = CustomerTask.findByTaskUUID(fakeTaskUUID);
    assertNotNull(ct);
    assertEquals(Restore, ct.getType());
    assertAuditEntry(1, defaultCustomer.uuid);
  }

  // For security reasons, performance reasons and DOS protection we should
  // continue to
  // impose some limit on request size. Here we test that sending request larger
  // that 500K will
  // cause us to return
  @Test
  public void testRestoreBackupRequestTooLarge() {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST5");
    BackupTableParams bp = new BackupTableParams();
    bp.storageConfigUUID = customerConfig.configUUID;
    bp.universeUUID = UUID.randomUUID();
    Backup.create(defaultCustomer.uuid, bp);
    ObjectNode bodyJson = Json.newObject();

    long maxReqSizeInBytes =
        app.config().getMemorySize("play.http.parser.maxMemoryBuffer").toBytes();
    String largeKeyspace = new String(new char[(int) (maxReqSizeInBytes)]).replace("\0", "#");
    bodyJson.put("keyspace", largeKeyspace);
    bodyJson.put("actionType", "RESTORE");
    bodyJson.put("storageConfigUUID", bp.storageConfigUUID.toString());
    bodyJson.put("storageLocation", "s3://foo/bar");

    int aproxPayloadLength = bodyJson.toString().length();
    assertTrue(
        "Actual (approx) payload size " + aproxPayloadLength,
        aproxPayloadLength > maxReqSizeInBytes && aproxPayloadLength < maxReqSizeInBytes + 1000);
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    Result result = restoreBackup(defaultUniverse.universeUUID, bodyJson, null);
    assertEquals(413, result.status());
    verify(mockCommissioner, never()).submit(any(), any());
  }

  @Test
  public void testRestoreBackupWithInvalidOwner() {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST5");
    BackupTableParams bp = new BackupTableParams();
    bp.storageConfigUUID = customerConfig.configUUID;
    bp.universeUUID = defaultUniverse.universeUUID;
    Backup.create(defaultCustomer.uuid, bp);
    ObjectNode bodyJson = Json.newObject();

    bodyJson.put("keyspace", "keyspace");
    bodyJson.put("actionType", "RESTORE");
    bodyJson.put("storageConfigUUID", bp.storageConfigUUID.toString());
    bodyJson.put("storageLocation", "s3://foo/bar");
    bodyJson.put("newOwner", "asgjf;jdsnc");

    Result result =
        assertPlatformException(() -> restoreBackup(defaultUniverse.universeUUID, bodyJson, null));
    assertEquals(BAD_REQUEST, result.status());
    verify(mockCommissioner, never()).submit(any(), any());

    bodyJson.put("newOwner", "$jdsnc");
    result =
        assertPlatformException(() -> restoreBackup(defaultUniverse.universeUUID, bodyJson, null));
    assertEquals(BAD_REQUEST, result.status());
    verify(mockCommissioner, never()).submit(any(), any());

    bodyJson.put("newOwner", "jdsn$c");
    result =
        assertPlatformException(() -> restoreBackup(defaultUniverse.universeUUID, bodyJson, null));
    assertEquals(BAD_REQUEST, result.status());
    verify(mockCommissioner, never()).submit(any(), any());

    bodyJson.put("newOwner", "jdsnc*");
    result =
        assertPlatformException(() -> restoreBackup(defaultUniverse.universeUUID, bodyJson, null));
    assertEquals(BAD_REQUEST, result.status());
    verify(mockCommissioner, never()).submit(any(), any());

    bodyJson.put("newOwner", "&");
    result =
        assertPlatformException(() -> restoreBackup(defaultUniverse.universeUUID, bodyJson, null));
    assertEquals(BAD_REQUEST, result.status());
    verify(mockCommissioner, never()).submit(any(), any());

    bodyJson.put("newOwner", "sjdachk|dkjsbfc");
    result =
        assertPlatformException(() -> restoreBackup(defaultUniverse.universeUUID, bodyJson, null));
    assertEquals(BAD_REQUEST, result.status());
    verify(mockCommissioner, never()).submit(any(), any());

    bodyJson.put("newOwner", "sjdachk dkjsbfc");
    result =
        assertPlatformException(() -> restoreBackup(defaultUniverse.universeUUID, bodyJson, null));
    assertEquals(BAD_REQUEST, result.status());
    verify(mockCommissioner, never()).submit(any(), any());

    bodyJson.put("newOwner", "sjdachk\ndkjsbfc");
    result =
        assertPlatformException(() -> restoreBackup(defaultUniverse.universeUUID, bodyJson, null));
    assertEquals(BAD_REQUEST, result.status());
    verify(mockCommissioner, never()).submit(any(), any());

    bodyJson.put("newOwner", "sjdachk\tdkjsbfc");
    result =
        assertPlatformException(() -> restoreBackup(defaultUniverse.universeUUID, bodyJson, null));
    assertEquals(BAD_REQUEST, result.status());
    verify(mockCommissioner, never()).submit(any(), any());

    bodyJson.put("newOwner", "sjdachk\tdkjsbfc");
    result =
        assertPlatformException(() -> restoreBackup(defaultUniverse.universeUUID, bodyJson, null));
    assertEquals(BAD_REQUEST, result.status());
    verify(mockCommissioner, never()).submit(any(), any());

    ArgumentCaptor<TaskType> taskType = ArgumentCaptor.forClass(TaskType.class);
    ArgumentCaptor<BackupTableParams> taskParams = ArgumentCaptor.forClass(BackupTableParams.class);

    bodyJson.put("newOwner", "yugabyte");
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    result = restoreBackup(defaultUniverse.universeUUID, bodyJson, null);
    verify(mockCommissioner, times(1)).submit(taskType.capture(), taskParams.capture());
    assertEquals(TaskType.BackupUniverse, taskType.getValue());
    assertOk(result);
    JsonNode resultJson = Json.parse(contentAsString(result));
    assertValue(resultJson, "taskUUID", fakeTaskUUID.toString());
    CustomerTask ct = CustomerTask.findByTaskUUID(fakeTaskUUID);
    assertNotNull(ct);
    assertEquals(Restore, ct.getType());
    assertAuditEntry(1, defaultCustomer.uuid);
  }

  @Test
  public void testDeleteBackup() {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST6");
    BackupTableParams bp = new BackupTableParams();
    bp.storageConfigUUID = customerConfig.configUUID;
    bp.universeUUID = defaultUniverse.universeUUID;
    Backup backup = Backup.create(defaultCustomer.uuid, bp);
    backup.transitionState(BackupState.Completed);
    List<String> backupUUIDList = new ArrayList<>();
    backupUUIDList.add(backup.backupUUID.toString());
    UUID fakeTaskUUID = UUID.randomUUID();
    ObjectNode resultNode = Json.newObject();
    when(mockTaskManager.isDuplicateDeleteBackupTask(defaultCustomer.uuid, backup.backupUUID))
        .thenReturn(false);
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    ArrayNode arrayNode = resultNode.putArray("backupUUID");
    for (String item : backupUUIDList) {
      arrayNode.add(item);
    }
    Result result = deleteBackup(resultNode, null);
    assertEquals(200, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    CustomerTask customerTask = CustomerTask.findByTaskUUID(fakeTaskUUID);
    assertEquals(customerTask.getTargetUUID(), backup.getBackupInfo().universeUUID);
    assertEquals(json.get("taskUUID").size(), 1);
    assertAuditEntry(1, defaultCustomer.uuid);
  }

  @Test
  public void testDeleteFailedBackup() {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST6");
    BackupTableParams bp = new BackupTableParams();
    bp.storageConfigUUID = customerConfig.configUUID;
    bp.universeUUID = defaultUniverse.universeUUID;
    Backup backup = Backup.create(defaultCustomer.uuid, bp);
    backup.transitionState(BackupState.Failed);
    List<String> backupUUIDList = new ArrayList<>();
    backupUUIDList.add(backup.backupUUID.toString());
    UUID fakeTaskUUID = UUID.randomUUID();
    ObjectNode resultNode = Json.newObject();
    when(mockTaskManager.isDuplicateDeleteBackupTask(defaultCustomer.uuid, backup.backupUUID))
        .thenReturn(false);
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    ArrayNode arrayNode = resultNode.putArray("backupUUID");
    for (String item : backupUUIDList) {
      arrayNode.add(item);
    }
    Result result = deleteBackup(resultNode, null);
    assertEquals(200, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    CustomerTask customerTask = CustomerTask.findByTaskUUID(fakeTaskUUID);
    assertEquals(customerTask.getTargetUUID(), backup.getBackupInfo().universeUUID);
    assertEquals(json.get("taskUUID").size(), 1);
    assertAuditEntry(1, defaultCustomer.uuid);
  }

  @Test
  @Parameters({"Failed", "Skipped", "FailedToDelete", "Stopped", "Completed"})
  @TestCaseName("testDeleteBackupYbWithValidStateWhenState:{0} ")
  public void testDeleteBackupYbWithValidState(BackupState state) {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST6");
    BackupTableParams bp = new BackupTableParams();
    bp.storageConfigUUID = customerConfig.configUUID;
    bp.universeUUID = UUID.randomUUID();
    Backup backup = Backup.create(defaultCustomer.uuid, bp);
    backup.transitionState(state);
    List<String> backupUUIDList = new ArrayList<>();
    backupUUIDList.add(backup.backupUUID.toString());
    UUID fakeTaskUUID = UUID.randomUUID();
    ObjectNode resultNode = Json.newObject();
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    ArrayNode arrayNode = resultNode.putArray("backups");
    for (String item : backupUUIDList) {
      ObjectNode deleteBackupObject = Json.newObject();
      deleteBackupObject.put("backupUUID", item);
      arrayNode.add(deleteBackupObject);
    }
    Result result = deleteBackupYb(resultNode, null);
    assertEquals(200, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    CustomerTask customerTask = CustomerTask.findByTaskUUID(fakeTaskUUID);
    assertEquals(customerTask.getTargetUUID(), backup.universeUUID);
    assertEquals(json.get("taskUUID").size(), 1);
    assertAuditEntry(1, defaultCustomer.uuid);
  }

  @Test
  @Parameters({"InProgress", "DeleteInProgress", "QueuedForDeletion"})
  @TestCaseName("testDeleteBackupYbWithInvalidStateWhenState:{0}")
  public void testDeleteBackupYbWithInvalidState(BackupState state) {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST6");
    BackupTableParams bp = new BackupTableParams();
    bp.storageConfigUUID = customerConfig.configUUID;
    bp.universeUUID = UUID.randomUUID();
    Backup backup = Backup.create(defaultCustomer.uuid, bp);
    backup.transitionState(state);
    List<String> backupUUIDList = new ArrayList<>();
    backupUUIDList.add(backup.backupUUID.toString());
    ObjectNode resultNode = Json.newObject();
    ArrayNode arrayNode = resultNode.putArray("backups");
    for (String item : backupUUIDList) {
      ObjectNode deleteBackupObject = Json.newObject();
      deleteBackupObject.put("backupUUID", item);
      arrayNode.add(deleteBackupObject);
    }
    Result result = deleteBackupYb(resultNode, null);
    assertEquals(200, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(json.get("taskUUID").size(), 0);
  }

  @Test
  public void testDeleteBackupYbWithCustomCustomerStorageConfig() {
    UUID invalidStorageConfigUUID = UUID.randomUUID();
    BackupTableParams bp = new BackupTableParams();
    bp.storageConfigUUID = invalidStorageConfigUUID;
    bp.universeUUID = UUID.randomUUID();
    Backup backup = Backup.create(defaultCustomer.uuid, bp);
    backup.transitionState(BackupState.Completed);
    List<String> backupUUIDList = new ArrayList<>();
    backupUUIDList.add(backup.backupUUID.toString());
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    ObjectNode resultNode = Json.newObject();
    ArrayNode arrayNode = resultNode.putArray("backups");
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST6");
    for (String item : backupUUIDList) {
      ObjectNode deleteBackupObject = Json.newObject();
      deleteBackupObject.put("backupUUID", item);
      deleteBackupObject.put("storageConfigUUID", customerConfig.configUUID.toString());
      arrayNode.add(deleteBackupObject);
    }
    Result result = deleteBackupYb(resultNode, null);
    assertEquals(200, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    CustomerTask customerTask = CustomerTask.findByTaskUUID(fakeTaskUUID);
    assertEquals(customerTask.getTargetUUID(), backup.universeUUID);
    assertEquals(json.get("taskUUID").size(), 1);
    assertAuditEntry(1, defaultCustomer.uuid);
    backup = Backup.getOrBadRequest(defaultCustomer.uuid, backup.backupUUID);
    assertEquals(customerConfig.configUUID, backup.getBackupInfo().storageConfigUUID);
  }

  @Test
  public void testDeleteBackupDuplicateTask() {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST600");
    BackupTableParams bp = new BackupTableParams();
    bp.storageConfigUUID = customerConfig.configUUID;
    bp.universeUUID = defaultUniverse.universeUUID;
    Backup backup = Backup.create(defaultCustomer.uuid, bp);
    backup.transitionState(BackupState.Completed);
    List<String> backupUUIDList = new ArrayList<>();
    backupUUIDList.add(backup.backupUUID.toString());
    UUID fakeTaskUUID = UUID.randomUUID();
    ObjectNode resultNode = Json.newObject();
    when(mockTaskManager.isDuplicateDeleteBackupTask(defaultCustomer.uuid, backup.backupUUID))
        .thenReturn(true);
    ArrayNode arrayNode = resultNode.putArray("backupUUID");
    for (String item : backupUUIDList) {
      arrayNode.add(item);
    }
    Result result = assertPlatformException(() -> deleteBackup(resultNode, null));
    assertBadRequest(result, "Task to delete same backup already exists.");
  }

  @Test
  public void testStopBackup() throws IOException, InterruptedException, ExecutionException {
    ProcessBuilder processBuilderObject = new ProcessBuilder("test");
    Process process = processBuilderObject.start();
    Util.setPID(defaultBackup.backupUUID, process);

    taskInfo = new TaskInfo(TaskType.CreateTable);
    taskInfo.setTaskDetails(Json.newObject());
    taskInfo.setOwner("");
    taskInfo.setTaskUUID(taskUUID);
    taskInfo.save();

    defaultBackup.setTaskUUID(taskUUID);
    ExecutorService executorService = Executors.newSingleThreadExecutor();

    Callable<Result> callable =
        () -> {
          return stopBackup(null, defaultBackup.backupUUID);
        };
    Future<Result> future = executorService.submit(callable);
    Thread.sleep(1000);
    taskInfo.setTaskState(State.Failure);
    taskInfo.save();

    Result result = future.get();
    executorService.shutdown();
    assertEquals(200, result.status());
    assertAuditEntry(1, defaultCustomer.uuid);
  }

  @Test
  public void testStopBackupCompleted() {
    defaultBackup.transitionState(BackupState.Completed);
    Result result =
        assertThrows(
                PlatformServiceException.class, () -> stopBackup(null, defaultBackup.backupUUID))
            .buildResult();
    assertEquals(400, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(json.get("error").asText(), "The process you want to stop is not in progress.");
  }

  @Test
  public void testStopBackupMaxRetry() throws IOException {
    ProcessBuilder processBuilderObject = new ProcessBuilder("test");
    Process process = processBuilderObject.start();
    Util.setPID(defaultBackup.backupUUID, process);

    taskInfo = new TaskInfo(TaskType.CreateTable);
    taskInfo.setTaskDetails(Json.newObject());
    taskInfo.setOwner("");
    taskInfo.setTaskUUID(taskUUID);
    taskInfo.save();

    defaultBackup.setTaskUUID(taskUUID);
    Result result =
        assertThrows(
                PlatformServiceException.class, () -> stopBackup(null, defaultBackup.backupUUID))
            .buildResult();
    taskInfo.save();

    assertEquals(400, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(
        json.get("error").asText(), "WaitFor task exceeded maxRetries! Task state is Created");
  }

  @Test
  public void testEditBackupWithStateNotComplete() {
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("timeBeforeDeleteFromPresentInMillis", 86400000L);

    Result result =
        assertPlatformException(() -> editBackup(defaultUser, bodyJson, defaultBackup.backupUUID));
    assertBadRequest(result, "Cannot edit a backup that is in progress state");
  }

  @Test
  public void testEditBackupWithNonPositiveDeletionTime() {
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("timeBeforeDeleteFromPresentInMillis", -1L);

    Result result =
        assertPlatformException(() -> editBackup(defaultUser, bodyJson, defaultBackup.backupUUID));
    assertEquals(BAD_REQUEST, result.status());
    assertBadRequest(
        result, "Please provide either a positive expiry time or storage config to edit backup");

    bodyJson.put("timeBeforeDeleteFromPresentInMillis", 0L);
    result =
        assertPlatformException(() -> editBackup(defaultUser, bodyJson, defaultBackup.backupUUID));
    assertBadRequest(
        result, "Please provide either a positive expiry time or storage config to edit backup");
  }

  @Test
  public void testEditBackup() {

    defaultBackup.state = BackupState.Completed;
    defaultBackup.update();
    Backup backup = Backup.getOrBadRequest(defaultCustomer.uuid, defaultBackup.backupUUID);
    // assertTrue(backup.state.equals(BackupState.Completed));

    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("timeBeforeDeleteFromPresentInMillis", 86400000L);
    bodyJson.put("expiryTimeUnit", "DAYS");
    Result result = editBackup(defaultUser, bodyJson, defaultBackup.backupUUID);
    backup = Backup.getOrBadRequest(defaultCustomer.uuid, defaultBackup.backupUUID);
    long afterTimeInMillis = System.currentTimeMillis() + 86400000L;
    long beforeTimeInMillis = System.currentTimeMillis() + 85400000L;

    long expiryTimeInMillis = backup.getExpiry().getTime();
    assertTrue(expiryTimeInMillis > beforeTimeInMillis);
    assertTrue(afterTimeInMillis > expiryTimeInMillis);
  }

  @Test
  public void testEditBackupWithoutTimeUnit() {

    defaultBackup.state = BackupState.Completed;
    defaultBackup.update();
    Backup backup = Backup.getOrBadRequest(defaultCustomer.uuid, defaultBackup.backupUUID);
    // assertTrue(backup.state.equals(BackupState.Completed));

    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("timeBeforeDeleteFromPresentInMillis", 86400000L);
    Result result =
        assertPlatformException(() -> editBackup(defaultUser, bodyJson, defaultBackup.backupUUID));
    assertBadRequest(result, "Please provide a time unit for backup expiry");
  }

  @Test
  public void testEditStorageConfigSuccess() {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST7");
    BackupTableParams bp = new BackupTableParams();
    bp.storageConfigUUID = customerConfig.configUUID;
    bp.universeUUID = UUID.randomUUID();
    Backup backup = Backup.create(defaultCustomer.uuid, bp);
    backup.transitionState(BackupState.Completed);
    UUID invalidConfigUUID = UUID.randomUUID();
    backup.updateStorageConfigUUID(invalidConfigUUID);
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("storageConfigUUID", customerConfig.configUUID.toString());
    Result result = editBackup(defaultUser, bodyJson, backup.backupUUID);
    assertOk(result);
    assertAuditEntry(1, defaultCustomer.uuid);
    backup.refresh();
    assertEquals(customerConfig.configUUID, backup.storageConfigUUID);
    assertEquals(customerConfig.configUUID, backup.getBackupInfo().storageConfigUUID);
  }

  @Test
  public void testEditStorageConfigWithAlreadyActiveConfig() {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST8");
    BackupTableParams bp = new BackupTableParams();
    bp.storageConfigUUID = customerConfig.configUUID;
    bp.universeUUID = UUID.randomUUID();
    Backup backup = Backup.create(defaultCustomer.uuid, bp);
    backup.transitionState(BackupState.Completed);
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("storageConfigUUID", customerConfig.configUUID.toString());
    Result result =
        assertPlatformException(() -> editBackup(defaultUser, bodyJson, backup.backupUUID));
    assertBadRequest(result, "Active storage config is already assigned to the backup");
    assertAuditEntry(0, defaultCustomer.uuid);
    backup.refresh();
    assertEquals(customerConfig.configUUID, backup.storageConfigUUID);
    assertEquals(customerConfig.configUUID, backup.getBackupInfo().storageConfigUUID);
  }

  @Test
  @Parameters({"InProgress", "DeleteInProgress", "QueuedForDeletion"})
  @TestCaseName("testEditStorageConfigWithBackupInState:{0} ")
  public void testEditStorageConfigWithBackpInProgressState(BackupState state) {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST9");
    BackupTableParams bp = new BackupTableParams();
    bp.storageConfigUUID = customerConfig.configUUID;
    UUID invalidConfigUUID = UUID.randomUUID();
    bp.universeUUID = UUID.randomUUID();
    Backup backup = Backup.create(defaultCustomer.uuid, bp);
    backup.transitionState(state);
    backup.updateStorageConfigUUID(invalidConfigUUID);
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("storageConfigUUID", customerConfig.configUUID.toString());
    Result result =
        assertPlatformException(() -> editBackup(defaultUser, bodyJson, backup.backupUUID));
    assertBadRequest(result, "Cannot edit a backup that is in progress state");
    assertAuditEntry(0, defaultCustomer.uuid);
    backup.refresh();
    assertEquals(invalidConfigUUID, backup.storageConfigUUID);
    assertEquals(invalidConfigUUID, backup.getBackupInfo().storageConfigUUID);
  }

  @Test
  public void testEditStorageConfigWithQueuedForDeletionConfig() {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST10");
    BackupTableParams bp = new BackupTableParams();
    bp.storageConfigUUID = customerConfig.configUUID;
    UUID invalidConfigUUID = UUID.randomUUID();
    bp.universeUUID = UUID.randomUUID();
    Backup backup = Backup.create(defaultCustomer.uuid, bp);
    backup.transitionState(BackupState.Completed);
    backup.updateStorageConfigUUID(invalidConfigUUID);
    customerConfig.setState(ConfigState.QueuedForDeletion);
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("storageConfigUUID", customerConfig.configUUID.toString());
    Result result =
        assertPlatformException(() -> editBackup(defaultUser, bodyJson, backup.backupUUID));
    assertBadRequest(result, "");
    assertAuditEntry(0, defaultCustomer.uuid);
    backup.refresh();
    assertEquals(invalidConfigUUID, backup.storageConfigUUID);
    assertEquals(invalidConfigUUID, backup.getBackupInfo().storageConfigUUID);
  }

  @Test
  public void testEditStorageConfigWithInvalidStorageConfigType() {
    CustomerConfig customerConfig = ModelFactory.createGcsStorageConfig(defaultCustomer, "TEST11");
    BackupTableParams bp = new BackupTableParams();
    bp.storageConfigUUID = customerConfig.configUUID;
    UUID invalidConfigUUID = UUID.randomUUID();
    bp.universeUUID = UUID.randomUUID();
    Backup backup = Backup.create(defaultCustomer.uuid, bp);
    backup.transitionState(BackupState.Completed);
    backup.updateStorageConfigUUID(invalidConfigUUID);
    CustomerConfig newConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST12");
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("storageConfigUUID", newConfig.configUUID.toString());
    Result result =
        assertPlatformException(() -> editBackup(defaultUser, bodyJson, backup.backupUUID));
    assertBadRequest(
        result,
        "Cannot assign "
            + newConfig.name
            + " type config to the backup stored in "
            + customerConfig.name);
    assertAuditEntry(0, defaultCustomer.uuid);
    backup.refresh();
    assertEquals(invalidConfigUUID, backup.storageConfigUUID);
    assertEquals(invalidConfigUUID, backup.getBackupInfo().storageConfigUUID);
  }

  @Test
  public void testEditStorageConfigWithInvalidConfigType() {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST13");
    BackupTableParams bp = new BackupTableParams();
    bp.storageConfigUUID = customerConfig.configUUID;
    UUID invalidConfigUUID = UUID.randomUUID();
    bp.universeUUID = UUID.randomUUID();
    Backup backup = Backup.create(defaultCustomer.uuid, bp);
    backup.transitionState(BackupState.Completed);
    backup.updateStorageConfigUUID(invalidConfigUUID);
    CustomerConfig newConfig = ModelFactory.setCallhomeLevel(defaultCustomer, "default");
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("storageConfigUUID", newConfig.configUUID.toString());
    Result result =
        assertPlatformException(() -> editBackup(defaultUser, bodyJson, backup.backupUUID));
    assertBadRequest(
        result, "Cannot assign " + newConfig.type + " type config in place of Storage Config");
    assertAuditEntry(0, defaultCustomer.uuid);
    backup.refresh();
    assertEquals(invalidConfigUUID, backup.storageConfigUUID);
    assertEquals(invalidConfigUUID, backup.getBackupInfo().storageConfigUUID);
  }

  @Test
  public void testEditStorageConfigWithInvalidConfig() {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST14");
    BackupTableParams bp = new BackupTableParams();
    bp.storageConfigUUID = customerConfig.configUUID;
    bp.universeUUID = UUID.randomUUID();
    Backup backup = Backup.create(defaultCustomer.uuid, bp);
    backup.transitionState(BackupState.Completed);
    UUID invalidConfigUUID = UUID.randomUUID();
    backup.updateStorageConfigUUID(invalidConfigUUID);
    doThrow(
            new PlatformServiceException(
                BAD_REQUEST, "Storage config TEST14 cannot access backup locations"))
        .when(mockBackupUtil)
        .validateStorageConfigOnBackup(any(), any());
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("storageConfigUUID", customerConfig.configUUID.toString());
    Result result =
        assertPlatformException(() -> editBackup(defaultUser, bodyJson, backup.backupUUID));
    assertBadRequest(result, "Storage config TEST14 cannot access backup locations");
    assertAuditEntry(0, defaultCustomer.uuid);
    backup.refresh();
    assertEquals(invalidConfigUUID, backup.storageConfigUUID);
    assertEquals(invalidConfigUUID, backup.getBackupInfo().storageConfigUUID);
  }

  @Test
  public void testInvalidEditBackupTaskParams() {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST14");
    BackupTableParams bp = new BackupTableParams();
    bp.storageConfigUUID = customerConfig.configUUID;
    bp.universeUUID = UUID.randomUUID();
    Backup backup = Backup.create(defaultCustomer.uuid, bp);
    backup.transitionState(BackupState.Completed);
    UUID invalidConfigUUID = UUID.randomUUID();
    backup.updateStorageConfigUUID(invalidConfigUUID);
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("timeBeforeDeleteFromPresentInMillis", "0");
    Result result =
        assertPlatformException(() -> editBackup(defaultUser, bodyJson, backup.backupUUID));
    assertBadRequest(
        result, "Please provide either a positive expiry time or storage config to edit backup");
    assertAuditEntry(0, defaultCustomer.uuid);
    backup.refresh();
    assertEquals(invalidConfigUUID, backup.storageConfigUUID);
    assertEquals(invalidConfigUUID, backup.getBackupInfo().storageConfigUUID);
  }

  private Result setThrottleParams(ObjectNode bodyJson, UUID universeUUID) {
    String authToken = defaultUser.createAuthToken();
    String method = "POST";
    String url =
        "/api/customers/"
            + defaultCustomer.uuid
            + "/universes/"
            + universeUUID.toString()
            + "/ybc_throttle_params";
    return FakeApiHelper.doRequestWithAuthTokenAndBody(method, url, authToken, bodyJson);
  }

  private Result getThrottleParams(UUID universeUUID) {
    String authToken = defaultUser.createAuthToken();
    String method = "GET";
    String url =
        "/api/customers/"
            + defaultCustomer.uuid
            + "/universes/"
            + universeUUID.toString()
            + "/ybc_throttle_params";
    return FakeApiHelper.doRequestWithAuthToken(method, url, authToken);
  }

  @Test
  public void testSetThrottleParamsSuccess() {
    Universe universe = ModelFactory.createUniverse("TEST1", defaultCustomer.uuid);
    UniverseDefinitionTaskParams details = universe.getUniverseDetails();
    ObjectNode bodyJson = Json.newObject();
    details.ybcInstalled = true;
    universe.setUniverseDetails(details);
    universe.save();
    doNothing().when(mockYbcManager).setThrottleParams(any(), any());
    Result result = setThrottleParams(bodyJson, universe.universeUUID);
    assertOk(result);
  }

  @Test
  public void testSetThrottleParamsFailedUniverseLocked() {
    Universe universe = ModelFactory.createUniverse("TEST2", defaultCustomer.uuid);
    UniverseDefinitionTaskParams details = universe.getUniverseDetails();
    ObjectNode bodyJson = Json.newObject();
    details.ybcInstalled = true;
    details.updateInProgress = true;
    universe.setUniverseDetails(details);
    universe.save();
    Result result =
        assertPlatformException(() -> setThrottleParams(bodyJson, universe.universeUUID));
    assertBadRequest(result, "Cannot set throttle params, universe task in progress.");
  }

  @Test
  public void testSetThrottleParamsFailedUniversePaused() {
    Universe universe = ModelFactory.createUniverse("TEST3", defaultCustomer.uuid);
    UniverseDefinitionTaskParams details = universe.getUniverseDetails();
    ObjectNode bodyJson = Json.newObject();
    details.ybcInstalled = true;
    details.universePaused = true;
    universe.setUniverseDetails(details);
    universe.save();
    Result result =
        assertPlatformException(() -> setThrottleParams(bodyJson, universe.universeUUID));
    assertBadRequest(result, "Cannot set throttle params, universe is paused.");
  }

  @Test
  public void testSetThrottleParamsFailedBackupInProgress() {
    Universe universe = ModelFactory.createUniverse("TEST4", defaultCustomer.uuid);
    UniverseDefinitionTaskParams details = universe.getUniverseDetails();
    details.ybcInstalled = true;
    ObjectNode bodyJson = Json.newObject();
    details.backupInProgress = true;
    universe.setUniverseDetails(details);
    universe.save();
    Result result =
        assertPlatformException(() -> setThrottleParams(bodyJson, universe.universeUUID));
    assertBadRequest(result, "Cannot set throttle params, universe task in progress.");
  }

  @Test
  public void testSetThrottleParamsTaskFailed() {
    Universe universe = ModelFactory.createUniverse("TEST5", defaultCustomer.uuid);
    UniverseDefinitionTaskParams details = universe.getUniverseDetails();
    details.ybcInstalled = true;
    ObjectNode bodyJson = Json.newObject();
    universe.setUniverseDetails(details);
    universe.save();
    doThrow(new RuntimeException("some failure"))
        .when(mockYbcManager)
        .setThrottleParams(any(), any());
    Result result =
        assertPlatformException(() -> setThrottleParams(bodyJson, universe.universeUUID));
    assertInternalServerError(
        result,
        String.format(
            "Got error setting throttle params for universe {}, error: {}",
            universe.universeUUID.toString(),
            "some failure"));
  }

  @Test
  public void testGetThrottleParamsSuccess() {
    Universe universe = ModelFactory.createUniverse("TEST6", defaultCustomer.uuid);
    UniverseDefinitionTaskParams details = universe.getUniverseDetails();
    details.ybcInstalled = true;
    universe.setUniverseDetails(details);
    universe.save();
    Map<String, String> tP = new HashMap<>();
    tP.put("foo", "bar");
    when(mockYbcManager.getThrottleParams(any())).thenReturn(tP);
    Result result = getThrottleParams(universe.universeUUID);
    assertOk(result);
    assertValues(Json.toJson(contentAsString(result)), "foo", ImmutableList.of("bar"));
  }

  @Test
  public void testGetThrottleParamsFailedUniversePaused() {
    Universe universe = ModelFactory.createUniverse("TEST7", defaultCustomer.uuid);
    UniverseDefinitionTaskParams details = universe.getUniverseDetails();
    details.ybcInstalled = true;
    details.universePaused = true;
    universe.setUniverseDetails(details);
    universe.save();
    Result result = assertPlatformException(() -> getThrottleParams(universe.universeUUID));
    assertBadRequest(result, "Cannot get throttle params, universe is paused.");
  }

  @Test
  public void testGetThrottleParamsTaskFailed() {
    Universe universe = ModelFactory.createUniverse("TEST8", defaultCustomer.uuid);
    UniverseDefinitionTaskParams details = universe.getUniverseDetails();
    details.ybcInstalled = true;
    universe.setUniverseDetails(details);
    universe.save();
    doThrow(new RuntimeException("some failure")).when(mockYbcManager).getThrottleParams(any());
    Result result = assertPlatformException(() -> getThrottleParams(universe.universeUUID));
    assertInternalServerError(
        result,
        String.format(
            "Got error getting throttle params for universe {}, error: {}",
            universe.universeUUID.toString(),
            "some failure"));
  }

  @Test
  public void testDeleteFailedIncrementalBackup() {
    BackupTableParams bp = new BackupTableParams();
    bp.storageConfigUUID = customerConfig.configUUID;
    bp.universeUUID = UUID.randomUUID();
    bp.baseBackupUUID = UUID.randomUUID();
    Backup backup = Backup.create(defaultCustomer.uuid, bp);
    backup.transitionState(BackupState.Failed);
    List<String> backupUUIDList = new ArrayList<>();
    backupUUIDList.add(backup.backupUUID.toString());
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    ObjectNode resultNode = Json.newObject();
    ArrayNode arrayNode = resultNode.putArray("backups");
    for (String item : backupUUIDList) {
      ObjectNode deleteBackupObject = Json.newObject();
      deleteBackupObject.put("backupUUID", item);
      arrayNode.add(deleteBackupObject);
    }
    Result result = deleteBackupYb(resultNode, null);
    assertEquals(200, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    CustomerTask customerTask = CustomerTask.findByTaskUUID(fakeTaskUUID);
    assertEquals(customerTask.getTargetUUID(), backup.universeUUID);
    assertEquals(json.get("taskUUID").size(), 1);
    assertAuditEntry(1, defaultCustomer.uuid);
  }

  @Test
  public void testDeleteCompletedIncrementalBackup() {
    BackupTableParams bp = new BackupTableParams();
    bp.storageConfigUUID = customerConfig.configUUID;
    bp.universeUUID = UUID.randomUUID();
    bp.baseBackupUUID = UUID.randomUUID();
    Backup backup = Backup.create(defaultCustomer.uuid, bp);
    backup.transitionState(BackupState.Completed);
    List<String> backupUUIDList = new ArrayList<>();
    backupUUIDList.add(backup.backupUUID.toString());
    ObjectNode resultNode = Json.newObject();
    ArrayNode arrayNode = resultNode.putArray("backups");
    for (String item : backupUUIDList) {
      ObjectNode deleteBackupObject = Json.newObject();
      deleteBackupObject.put("backupUUID", item);
      arrayNode.add(deleteBackupObject);
    }
    Result result = deleteBackupYb(resultNode, null);
    assertEquals(200, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(json.get("taskUUID").size(), 0);
    assertAuditEntry(1, defaultCustomer.uuid);
  }
}
