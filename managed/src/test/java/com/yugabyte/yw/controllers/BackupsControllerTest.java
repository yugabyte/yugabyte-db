// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.AssertHelper.assertAuditEntry;
import static com.yugabyte.yw.common.AssertHelper.assertErrorNodeValue;
import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static com.yugabyte.yw.common.AssertHelper.assertValue;
import static com.yugabyte.yw.common.AssertHelper.assertValues;
import static com.yugabyte.yw.common.AssertHelper.assertBadRequest;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static com.yugabyte.yw.models.CustomerTask.TaskType.Restore;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.Mockito.any;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.FORBIDDEN;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.common.FakeApiHelper;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Backup.BackupState;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerConfig;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.TaskInfo.State;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.helpers.TaskType;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;
import java.util.concurrent.Callable;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import org.junit.Before;
import org.junit.Test;
import org.mockito.ArgumentCaptor;
import play.libs.Json;
import play.mvc.Result;

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

  private Result deleteBackup(ObjectNode bodyJson, Users user) {
    String authToken = user == null ? defaultUser.createAuthToken() : user.createAuthToken();
    String method = "DELETE";
    String url = "/api/customers/" + defaultCustomer.uuid + "/backups";
    return FakeApiHelper.doRequestWithAuthTokenAndBody(method, url, authToken, bodyJson);
  }

  private Result stopBackup(Users user, UUID backupUUID) {
    String authToken = user == null ? defaultUser.createAuthToken() : user.createAuthToken();
    String method = "POST";
    String url = "/api/customers/" + defaultCustomer.uuid + "/backups/" + backupUUID + "/stop";
    return FakeApiHelper.doRequestWithAuthToken(method, url, authToken);
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
  public void testStopBackupCompleted()
      throws IOException, InterruptedException, ExecutionException {
    defaultBackup.transitionState(BackupState.Completed);
    Result result =
        assertThrows(
                PlatformServiceException.class, () -> stopBackup(null, defaultBackup.backupUUID))
            .getResult();
    assertEquals(400, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(json.get("error").asText(), "The process you want to stop is not in progress.");
  }

  @Test
  public void testStopBackupMaxRetry()
      throws IOException, InterruptedException, ExecutionException {
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
            .getResult();
    taskInfo.save();

    assertEquals(400, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(
        json.get("error").asText(), "WaitFor task exceeded maxRetries! Task state is Created");
  }
}
