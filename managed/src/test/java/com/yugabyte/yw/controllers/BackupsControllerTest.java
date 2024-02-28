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
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyLong;
import static org.mockito.Mockito.doNothing;
import static org.mockito.Mockito.doThrow;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.FORBIDDEN;
import static play.mvc.Http.Status.OK;
import static play.mvc.Http.Status.PRECONDITION_FAILED;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.customer.config.CustomerConfigService;
import com.yugabyte.yw.common.rbac.Permission;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.forms.BackupRequestParams;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.forms.RestoreBackupParams;
import com.yugabyte.yw.forms.RestoreBackupParams.BackupStorageInfo;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.YbcThrottleParametersResponse;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Backup.BackupState;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.Restore;
import com.yugabyte.yw.models.RuntimeConfigEntry;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.TaskInfo.State;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.configs.CustomerConfig.ConfigState;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.models.helpers.TimeUnit;
import com.yugabyte.yw.models.rbac.ResourceGroup;
import com.yugabyte.yw.models.rbac.ResourceGroup.ResourceDefinition;
import com.yugabyte.yw.models.rbac.Role;
import com.yugabyte.yw.models.rbac.Role.RoleType;
import com.yugabyte.yw.models.rbac.RoleBinding;
import com.yugabyte.yw.models.rbac.RoleBinding.RoleBindingType;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashSet;
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
import org.springframework.test.util.ReflectionTestUtils;
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
  private Role role;
  private ResourceDefinition rd1;
  private UUID fakeTaskUUID;

  Permission permission1 = new Permission(ResourceType.UNIVERSE, Action.BACKUP_RESTORE);

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    defaultUser = ModelFactory.testUser(defaultCustomer);
    defaultUniverse = ModelFactory.createUniverse(defaultCustomer.getId());
    defaultUniverse = ModelFactory.addNodesToUniverse(defaultUniverse.getUniverseUUID(), 1);
    role =
        Role.create(
            defaultCustomer.getUuid(),
            "FakeRole1",
            "testDescription",
            RoleType.Custom,
            new HashSet<>(Arrays.asList(permission1)));
    rd1 =
        ResourceDefinition.builder()
            .resourceType(ResourceType.UNIVERSE)
            .resourceUUIDSet(new HashSet<>(Arrays.asList(defaultUniverse.getUniverseUUID())))
            .build();
    taskUUID = UUID.randomUUID();
    backupTableParams = new BackupTableParams();
    backupTableParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST105");
    backupTableParams.storageConfigUUID = customerConfig.getConfigUUID();
    backupTableParams.customerUuid = defaultCustomer.getUuid();
    defaultBackup = Backup.create(defaultCustomer.getUuid(), backupTableParams);
    defaultBackup.setTaskUUID(taskUUID);
    defaultBackup.save();

    RestoreBackupParams restoreBackupParams = new RestoreBackupParams();
    restoreBackupParams.customerUUID = defaultCustomer.getUuid();
    restoreBackupParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    restoreBackupParams.storageConfigUUID = customerConfig.getConfigUUID();
    restoreBackupParams.backupStorageInfoList = new ArrayList<BackupStorageInfo>();
    BackupStorageInfo storageInfo = new BackupStorageInfo();
    storageInfo.storageLocation = defaultBackup.getBackupInfo().storageLocation;
    storageInfo.keyspace = defaultBackup.getBackupInfo().getKeyspace();
    restoreBackupParams.backupStorageInfoList.add(storageInfo);
    ObjectMapper mapper = new ObjectMapper();
    JsonNode bodyJson = mapper.valueToTree(restoreBackupParams);
    TaskInfo taskInfo = new TaskInfo(TaskType.RestoreBackup, null);
    taskInfo.setDetails(bodyJson);
    taskInfo.setOwner("");
    UUID restoreTaskUUID = UUID.randomUUID();
    taskInfo.setTaskUUID(restoreTaskUUID);
    taskInfo.save();

    TaskInfo taskInfoSub = new TaskInfo(TaskType.RestoreBackupYb, null);
    taskInfoSub.setDetails(bodyJson);
    taskInfoSub.setOwner("");
    UUID taskUUIDSub = UUID.randomUUID();
    taskInfoSub.setTaskUUID(taskUUIDSub);
    taskInfoSub.setParentUuid(restoreTaskUUID);
    taskInfoSub.setPosition(1);
    taskInfoSub.save();
    Restore restore = Restore.create(restoreTaskUUID, restoreBackupParams);

    restore.setState(Restore.State.Created);
    restore.save();

    fakeTaskUUID = buildTaskInfo(null, TaskType.CreateBackupSchedule);
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
  }

  private JsonNode listBackups(UUID universeUUID) {
    String authToken = defaultUser.createAuthToken();
    String method = "GET";
    String url =
        "/api/customers/" + defaultCustomer.getUuid() + "/universes/" + universeUUID + "/backups";

    Result r = doRequestWithAuthToken(method, url, authToken);
    assertOk(r);
    return Json.parse(contentAsString(r));
  }

  @Test
  public void testListWithValidUniverse() {
    JsonNode resultJson = listBackups(defaultUniverse.getUniverseUUID());
    assertEquals(1, resultJson.size());
    assertValues(
        resultJson, "backupUUID", ImmutableList.of(defaultBackup.getBackupUUID().toString()));
    assertAuditEntry(0, defaultCustomer.getUuid());
  }

  @Test
  public void testListWithInvalidUniverse() {
    JsonNode resultJson = listBackups(UUID.randomUUID());
    assertEquals(0, resultJson.size());
    assertAuditEntry(0, defaultCustomer.getUuid());
  }

  @Test
  public void testListWithHiddenStorage() {
    JsonNode features =
        Json.parse(
            "{\"universes\": { \"details\": { \"backups\": { \"storageLocation\": \"hidden\"}}}}");
    defaultCustomer.upsertFeatures(features);
    assertEquals(features, defaultCustomer.getFeatures());

    BackupTableParams btp = new BackupTableParams();
    btp.setUniverseUUID(defaultUniverse.getUniverseUUID());
    btp.storageConfigUUID = UUID.randomUUID();
    Backup backup = Backup.create(defaultCustomer.getUuid(), btp);
    backup.setTaskUUID(taskUUID);
    // Patching manually. The broken backups left from previous releases, currently we can't create
    // such backups through API.
    btp.storageLocation = null;
    backup.setBackupInfo(btp);
    backup.save();

    JsonNode resultJson = listBackups(defaultUniverse.getUniverseUUID());
    assertEquals(2, resultJson.size());
    assertValues(
        resultJson,
        "backupUUID",
        ImmutableList.of(
            defaultBackup.getBackupUUID().toString(), backup.getBackupUUID().toString()));

    // Only one storageLocation should be in values as null values are filtered.
    assertValues(resultJson, "storageLocation", ImmutableList.of("**********"));
  }

  private JsonNode fetchBackupsbyTaskId(UUID universeUUID, UUID taskUUID) {
    String authToken = defaultUser.createAuthToken();
    String method = "GET";
    String url =
        "/api/customers/"
            + defaultCustomer.getUuid()
            + "/universes/"
            + universeUUID
            + "/backups/tasks/"
            + taskUUID;

    Result r = doRequestWithAuthToken(method, url, authToken);
    assertOk(r);
    return Json.parse(contentAsString(r));
  }

  @Test
  public void testFetchBackupsByTaskUUIDWithSingleEntry() {
    JsonNode resultJson = fetchBackupsbyTaskId(defaultUniverse.getUniverseUUID(), taskUUID);
    assertEquals(1, resultJson.size());
    assertValues(
        resultJson, "backupUUID", ImmutableList.of(defaultBackup.getBackupUUID().toString()));
    assertAuditEntry(0, defaultCustomer.getUuid());
  }

  @Test
  public void testCreateBackup() {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST21");
    CustomerConfigService mockCCS = mock(CustomerConfigService.class);
    when(mockCCS.getOrBadRequest(any(), any())).thenReturn(customerConfig);
    when(mockBackupHelper.createBackupTask(any(), any())).thenCallRealMethod();
    ReflectionTestUtils.setField(mockBackupHelper, "customerConfigService", mockCCS);
    ReflectionTestUtils.setField(mockBackupHelper, "commissioner", mockCommissioner);
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("universeUUID", defaultUniverse.getUniverseUUID().toString());
    bodyJson.put("backupType", "PGSQL_TABLE_TYPE");
    bodyJson.put("storageConfigUUID", customerConfig.getConfigUUID().toString());
    Result r = createBackupYb(bodyJson, null);
    JsonNode resultJson = Json.parse(contentAsString(r));
    assertValue(resultJson, "taskUUID", fakeTaskUUID.toString());
    assertEquals(OK, r.status());
    verify(mockCommissioner, times(1)).submit(any(), any());
  }

  @Test
  public void testCreateScheduledBackupAsyncValidCron() {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST22");
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("universeUUID", defaultUniverse.getUniverseUUID().toString());
    bodyJson.put("storageConfigUUID", customerConfig.getConfigUUID().toString());
    bodyJson.put("cronExpression", "0 */2 * * *");
    bodyJson.put("scheduleName", "schedule-1");
    bodyJson.put("backupType", "PGSQL_TABLE_TYPE");
    Result r = createBackupScheduleAsync(bodyJson, null);
    assertEquals(OK, r.status());
  }

  @Test
  public void testCreateScheduledBackupValidCron() {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST22");
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("universeUUID", defaultUniverse.getUniverseUUID().toString());
    bodyJson.put("storageConfigUUID", customerConfig.getConfigUUID().toString());
    bodyJson.put("cronExpression", "0 */2 * * *");
    bodyJson.put("scheduleName", "schedule-1");
    bodyJson.put("backupType", "PGSQL_TABLE_TYPE");
    Result r = createBackupSchedule(bodyJson, null);
    assertEquals(OK, r.status());
  }

  @Test
  public void testCreateBackupScheduleAsyncWithTimeUnit() {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST25");
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("universeUUID", defaultUniverse.getUniverseUUID().toString());
    bodyJson.put("storageConfigUUID", customerConfig.getConfigUUID().toString());
    bodyJson.put("schedulingFrequency", 10000000L);
    bodyJson.put("scheduleName", "schedule-1");
    bodyJson.put("frequencyTimeUnit", "HOURS");
    bodyJson.put("backupType", "PGSQL_TABLE_TYPE");
    Result r = createBackupScheduleAsync(bodyJson, null);
    assertEquals(OK, r.status());
  }

  @Test
  public void testCreateBackupScheduleWithTimeUnit() {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST25");
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("universeUUID", defaultUniverse.getUniverseUUID().toString());
    bodyJson.put("storageConfigUUID", customerConfig.getConfigUUID().toString());
    bodyJson.put("schedulingFrequency", 10000000L);
    bodyJson.put("scheduleName", "schedule-1");
    bodyJson.put("frequencyTimeUnit", "HOURS");
    bodyJson.put("backupType", "PGSQL_TABLE_TYPE");
    Result r = createBackupSchedule(bodyJson, null);
    assertEquals(OK, r.status());
  }

  @Test
  public void testCreateScheduleBackupAsyncWithoutName() {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST22");
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("universeUUID", defaultUniverse.getUniverseUUID().toString());
    bodyJson.put("storageConfigUUID", customerConfig.getConfigUUID().toString());
    bodyJson.put("cronExpression", "0 */2 * * *");
    bodyJson.put("backupType", "PGSQL_TABLE_TYPE");
    Result result = assertPlatformException(() -> createBackupScheduleAsync(bodyJson, null));
    assertBadRequest(result, "Provide a name for the schedule");
  }

  @Test
  public void testCreateScheduleBackupWithoutName() {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST22");
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("universeUUID", defaultUniverse.getUniverseUUID().toString());
    bodyJson.put("storageConfigUUID", customerConfig.getConfigUUID().toString());
    bodyJson.put("cronExpression", "0 */2 * * *");
    bodyJson.put("backupType", "PGSQL_TABLE_TYPE");
    Result result = assertPlatformException(() -> createBackupSchedule(bodyJson, null));
    assertBadRequest(result, "Provide a name for the schedule");
  }

  @Test
  public void testCreateScheduleBackupAsyncWithoutTimeUnit() {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST25");
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("universeUUID", defaultUniverse.getUniverseUUID().toString());
    bodyJson.put("storageConfigUUID", customerConfig.getConfigUUID().toString());
    bodyJson.put("schedulingFrequency", 10000000L);
    bodyJson.put("backupType", "PGSQL_TABLE_TYPE");
    bodyJson.put("scheduleName", "schedule-1");
    Result r = assertPlatformException(() -> createBackupScheduleAsync(bodyJson, null));
    assertBadRequest(r, "Please provide time unit for scheduler frequency");
  }

  @Test
  public void testCreateScheduleBackupWithoutTimeUnit() {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST25");
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("universeUUID", defaultUniverse.getUniverseUUID().toString());
    bodyJson.put("storageConfigUUID", customerConfig.getConfigUUID().toString());
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
    bodyJson.put("universeUUID", defaultUniverse.getUniverseUUID().toString());
    bodyJson.put("storageConfigUUID", customerConfig.getConfigUUID().toString());
    bodyJson.put("cronExpression", "0 */2 * * *");
    bodyJson.put("backupType", "PGSQL_TABLE_TYPE");
    bodyJson.put("scheduleName", "schedule-1");
    bodyJson.put("frequencyTimeUnit", "HOURS");
    bodyJson.put("schedulingFrequency", 1000000000L);
    BackupRequestParams params = new BackupRequestParams();
    Schedule.create(
        defaultCustomer.getUuid(),
        defaultUniverse.getUniverseUUID(),
        params,
        TaskType.CreateBackup,
        1000000000L,
        "0 */2 * * *",
        TimeUnit.HOURS,
        "schedule-1");
    Result result = assertPlatformException(() -> createBackupSchedule(bodyJson, null));
    assertBadRequest(result, "Schedule with name schedule-1 already exist");
  }

  @Test
  public void testCreateScheduleBackupAsyncWithDuplicateName() {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST22");
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("universeUUID", defaultUniverse.getUniverseUUID().toString());
    bodyJson.put("storageConfigUUID", customerConfig.getConfigUUID().toString());
    bodyJson.put("cronExpression", "0 */2 * * *");
    bodyJson.put("backupType", "PGSQL_TABLE_TYPE");
    bodyJson.put("scheduleName", "schedule-1");
    bodyJson.put("frequencyTimeUnit", "HOURS");
    bodyJson.put("schedulingFrequency", 1000000000L);
    BackupRequestParams params = new BackupRequestParams();
    Schedule.create(
        defaultCustomer.getUuid(),
        defaultUniverse.getUniverseUUID(),
        params,
        TaskType.CreateBackup,
        1000000000L,
        "0 */2 * * *",
        TimeUnit.HOURS,
        "schedule-1");
    Result result = assertPlatformException(() -> createBackupScheduleAsync(bodyJson, null));
    assertBadRequest(result, "Schedule with name schedule-1 already exist");
  }

  @Test
  public void testCreateScheduledBackupInvalid() {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST24");
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("universeUUID", defaultUniverse.getUniverseUUID().toString());
    bodyJson.put("storageConfigUUID", customerConfig.getConfigUUID().toString());
    bodyJson.put("scheduleName", "schedule-1");
    bodyJson.put("backupType", "PGSQL_TABLE_TYPE");
    Result r = assertPlatformException(() -> createBackupSchedule(bodyJson, null));
    JsonNode resultJson = Json.parse(contentAsString(r));
    assertValue(resultJson, "error", "Provide Cron Expression or Scheduling frequency");
    assertEquals(BAD_REQUEST, r.status());
    verify(mockCommissioner, times(0)).submit(any(), any());
  }

  @Test
  public void testCreateScheduledBackupAsyncInvalid() {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST24");
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("universeUUID", defaultUniverse.getUniverseUUID().toString());
    bodyJson.put("storageConfigUUID", customerConfig.getConfigUUID().toString());
    bodyJson.put("scheduleName", "schedule-1");
    bodyJson.put("backupType", "PGSQL_TABLE_TYPE");
    Result r = assertPlatformException(() -> createBackupScheduleAsync(bodyJson, null));
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
            defaultCustomer.getId(),
            CloudType.aws,
            null,
            null,
            true);
    bodyJson.put("universeUUID", universe.getUniverseUUID().toString());
    bodyJson.put("storageConfigUUID", customerConfig.getConfigUUID().toString());
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
  public void testCreateIncrementalScheduleBackupAsyncSuccess() {
    ObjectNode bodyJson = Json.newObject();
    Universe universe =
        ModelFactory.createUniverse(
            "Test-Universe-1",
            UUID.randomUUID(),
            defaultCustomer.getId(),
            CloudType.aws,
            null,
            null,
            true);
    bodyJson.put("universeUUID", universe.getUniverseUUID().toString());
    bodyJson.put("storageConfigUUID", customerConfig.getConfigUUID().toString());
    bodyJson.put("schedulingFrequency", 1000000000L);
    bodyJson.put("scheduleName", "schedule-1");
    bodyJson.put("frequencyTimeUnit", "HOURS");
    bodyJson.put("incrementalBackupFrequency", 10000000L);
    bodyJson.put("incrementalBackupFrequencyTimeUnit", "HOURS");
    bodyJson.put("backupType", "PGSQL_TABLE_TYPE");
    Result r = createBackupScheduleAsync(bodyJson, null);
    assertEquals(OK, r.status());
  }

  @Test
  public void testCreateIncrementalScheduleBackupWithOutFrequencyTimeUnit() {
    ObjectNode bodyJson = Json.newObject();
    Universe universe =
        ModelFactory.createUniverse(
            "Test-Universe-1",
            UUID.randomUUID(),
            defaultCustomer.getId(),
            CloudType.aws,
            null,
            null,
            true);
    bodyJson.put("universeUUID", universe.getUniverseUUID().toString());
    bodyJson.put("storageConfigUUID", customerConfig.getConfigUUID().toString());
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
  public void testCreateIncrementalScheduleBackupAsyncWithOutFrequencyTimeUnit() {
    ObjectNode bodyJson = Json.newObject();
    Universe universe =
        ModelFactory.createUniverse(
            "Test-Universe-1",
            UUID.randomUUID(),
            defaultCustomer.getId(),
            CloudType.aws,
            null,
            null,
            true);
    bodyJson.put("universeUUID", universe.getUniverseUUID().toString());
    bodyJson.put("storageConfigUUID", customerConfig.getConfigUUID().toString());
    bodyJson.put("schedulingFrequency", 1000000000L);
    bodyJson.put("scheduleName", "schedule-1");
    bodyJson.put("frequencyTimeUnit", "HOURS");
    bodyJson.put("incrementalBackupFrequency", 10000000L);
    bodyJson.put("backupType", "PGSQL_TABLE_TYPE");
    Result r = assertPlatformException(() -> createBackupScheduleAsync(bodyJson, null));
    JsonNode resultJson = Json.parse(contentAsString(r));
    assertValue(resultJson, "error", "Please provide time unit for incremental backup frequency.");
    assertEquals(BAD_REQUEST, r.status());
  }

  @Test
  public void testCreateIncrementalScheduleBackupOnNonYbcUniverse() {
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("universeUUID", defaultUniverse.getUniverseUUID().toString());
    bodyJson.put("storageConfigUUID", customerConfig.getConfigUUID().toString());
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
  public void testCreateIncrementalScheduleBackupAsyncOnNonYbcUniverse() {
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("universeUUID", defaultUniverse.getUniverseUUID().toString());
    bodyJson.put("storageConfigUUID", customerConfig.getConfigUUID().toString());
    bodyJson.put("schedulingFrequency", 1000000000L);
    bodyJson.put("scheduleName", "schedule-1");
    bodyJson.put("frequencyTimeUnit", "HOURS");
    bodyJson.put("incrementalBackupFrequency", 10000000L);
    bodyJson.put("incrementalBackupFrequencyTimeUnit", "HOURS");
    bodyJson.put("backupType", "PGSQL_TABLE_TYPE");
    Result r = assertPlatformException(() -> createBackupScheduleAsync(bodyJson, null));
    JsonNode resultJson = Json.parse(contentAsString(r));
    assertValue(
        resultJson, "error", "Cannot create incremental backup schedules on non-ybc universes.");
    assertEquals(BAD_REQUEST, r.status());
  }

  @Test
  public void testCreateIncrementalScheduleBackupWithInvalidIncrementalFrequency() {
    doThrow(
            new PlatformServiceException(
                BAD_REQUEST,
                "Incremental backup frequency should be lower than full backup frequency."))
        .when(mockBackupHelper)
        .validateIncrementalScheduleFrequency(anyLong(), anyLong(), any());
    ObjectNode bodyJson = Json.newObject();
    Universe universe =
        ModelFactory.createUniverse(
            "Test-Universe-1",
            UUID.randomUUID(),
            defaultCustomer.getId(),
            CloudType.aws,
            null,
            null,
            true);
    bodyJson.put("universeUUID", universe.getUniverseUUID().toString());
    bodyJson.put("storageConfigUUID", customerConfig.getConfigUUID().toString());
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
  }

  @Test
  public void testCreateIncrementalScheduleBackupAsyncWithInvalidIncrementalFrequency() {
    doThrow(
            new PlatformServiceException(
                BAD_REQUEST,
                "Incremental backup frequency should be lower than full backup frequency."))
        .when(mockBackupHelper)
        .validateIncrementalScheduleFrequency(anyLong(), anyLong(), any());
    ObjectNode bodyJson = Json.newObject();
    Universe universe =
        ModelFactory.createUniverse(
            "Test-Universe-1",
            UUID.randomUUID(),
            defaultCustomer.getId(),
            CloudType.aws,
            null,
            null,
            true);
    bodyJson.put("universeUUID", universe.getUniverseUUID().toString());
    bodyJson.put("storageConfigUUID", customerConfig.getConfigUUID().toString());
    bodyJson.put("cronExpression", "0 */2 * * *");
    bodyJson.put("scheduleName", "schedule-1");
    bodyJson.put("incrementalBackupFrequency", 10000000000L);
    bodyJson.put("incrementalBackupFrequencyTimeUnit", "HOURS");
    bodyJson.put("backupType", "PGSQL_TABLE_TYPE");
    Result r = assertPlatformException(() -> createBackupScheduleAsync(bodyJson, null));
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
    r = assertPlatformException(() -> createBackupScheduleAsync(bodyJson, null));
    resultJson = Json.parse(contentAsString(r));
    assertValue(
        resultJson,
        "error",
        "Incremental backup frequency should be lower than full backup frequency.");
    assertEquals(BAD_REQUEST, r.status());
  }

  @Test
  public void testCreateIncrementalScheduleBackupWithBaseBackup() {
    ObjectNode bodyJson = Json.newObject();
    Universe universe =
        ModelFactory.createUniverse(
            "Test-Universe-1",
            UUID.randomUUID(),
            defaultCustomer.getId(),
            CloudType.aws,
            null,
            null,
            true);
    bodyJson.put("universeUUID", universe.getUniverseUUID().toString());
    bodyJson.put("storageConfigUUID", customerConfig.getConfigUUID().toString());
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
  public void testCreateIncrementalScheduleBackupAsyncWithBaseBackup() {
    ObjectNode bodyJson = Json.newObject();
    Universe universe =
        ModelFactory.createUniverse(
            "Test-Universe-1",
            UUID.randomUUID(),
            defaultCustomer.getId(),
            CloudType.aws,
            null,
            null,
            true);
    bodyJson.put("universeUUID", universe.getUniverseUUID().toString());
    bodyJson.put("storageConfigUUID", customerConfig.getConfigUUID().toString());
    bodyJson.put("schedulingFrequency", 1000000000L);
    bodyJson.put("scheduleName", "schedule-1");
    bodyJson.put("frequencyTimeUnit", "HOURS");
    bodyJson.put("incrementalBackupFrequency", 10000000L);
    bodyJson.put("incrementalBackupFrequencyTimeUnit", "HOURS");
    bodyJson.put("baseBackupUUID", UUID.randomUUID().toString());
    bodyJson.put("backupType", "PGSQL_TABLE_TYPE");
    Result r = assertPlatformException(() -> createBackupScheduleAsync(bodyJson, null));
    JsonNode resultJson = Json.parse(contentAsString(r));
    assertValue(resultJson, "error", "Cannot assign base backup while creating backup schedules.");
    assertEquals(BAD_REQUEST, r.status());
  }

  @Test
  public void testCreateBackupValidationFailed() {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST25");
    CustomerConfigService mockCCS = mock(CustomerConfigService.class);
    when(mockCCS.getOrBadRequest(any(), any())).thenReturn(customerConfig);
    when(mockBackupHelper.createBackupTask(any(), any())).thenCallRealMethod();
    doThrow(new PlatformServiceException(PRECONDITION_FAILED, "error"))
        .when(mockBackupHelper)
        .validateStorageConfig(any());
    ReflectionTestUtils.setField(mockBackupHelper, "customerConfigService", mockCCS);
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("universeUUID", defaultUniverse.getUniverseUUID().toString());
    bodyJson.put("storageConfigUUID", customerConfig.getConfigUUID().toString());
    bodyJson.put("backupType", "PGSQL_TABLE_TYPE");
    Result r = assertPlatformException(() -> createBackupYb(bodyJson, null));
    JsonNode resultJson = Json.parse(contentAsString(r));
    assertValue(resultJson, "error", "error");
    assertEquals(PRECONDITION_FAILED, r.status());
    verify(mockCommissioner, times(0)).submit(any(), any());
  }

  @Test
  public void testCreateBackupWithUniverseDisabled() {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST25");
    ObjectNode bodyJson = Json.newObject();
    Map<String, String> config = defaultUniverse.getConfig();
    when(mockBackupHelper.createBackupTask(any(), any())).thenCallRealMethod();

    config.put(Universe.TAKE_BACKUPS, "false");
    defaultUniverse.updateConfig(config);
    defaultUniverse.update();
    bodyJson.put("universeUUID", defaultUniverse.getUniverseUUID().toString());
    bodyJson.put("storageConfigUUID", customerConfig.getConfigUUID().toString());
    bodyJson.put("backupType", "PGSQL_TABLE_TYPE");
    Result r = assertPlatformException(() -> createBackupYb(bodyJson, null));
    JsonNode resultJson = Json.parse(contentAsString(r));
    assertValue(resultJson, "error", "Taking backups on the universe is disabled");
    assertEquals(BAD_REQUEST, r.status());
    verify(mockCommissioner, times(0)).submit(any(), any());
  }

  @Test
  public void testCreateBackupWithoutTimeUnit() {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST26");
    when(mockBackupHelper.createBackupTask(any(), any())).thenCallRealMethod();
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("universeUUID", defaultUniverse.getUniverseUUID().toString());
    bodyJson.put("storageConfigUUID", customerConfig.getConfigUUID().toString());
    bodyJson.put("timeBeforeDelete", 100000L);
    bodyJson.put("backupType", "PGSQL_TABLE_TYPE");
    Result r = assertPlatformException(() -> createBackupYb(bodyJson, null));
    assertBadRequest(r, "Please provide time unit for backup expiry");
    verify(mockCommissioner, times(0)).submit(any(), any());
  }

  @Test
  public void testFetchBackupsByTaskUUIDWithMultipleEntries() {
    Backup backup2 = Backup.create(defaultCustomer.getUuid(), backupTableParams);
    backup2.setTaskUUID(taskUUID);
    backup2.save();

    JsonNode resultJson = fetchBackupsbyTaskId(defaultUniverse.getUniverseUUID(), taskUUID);
    assertEquals(2, resultJson.size());
    assertValues(
        resultJson,
        "backupUUID",
        ImmutableList.of(
            defaultBackup.getBackupUUID().toString(), backup2.getBackupUUID().toString()));
    assertAuditEntry(0, defaultCustomer.getUuid());
  }

  @Test
  public void testFetchBackupsByTaskUUIDWithDifferentTaskEntries() {
    Backup backup2 = Backup.create(defaultCustomer.getUuid(), backupTableParams);
    backup2.setTaskUUID(taskUUID);
    backup2.save();
    Backup backup3 = Backup.create(defaultCustomer.getUuid(), backupTableParams);
    backup3.setTaskUUID(UUID.randomUUID());
    backup3.save();

    JsonNode resultJson = fetchBackupsbyTaskId(defaultUniverse.getUniverseUUID(), taskUUID);
    assertEquals(2, resultJson.size());
    assertValues(
        resultJson,
        "backupUUID",
        ImmutableList.of(
            defaultBackup.getBackupUUID().toString(), backup2.getBackupUUID().toString()));
    assertAuditEntry(0, defaultCustomer.getUuid());
  }

  private Result restoreBackup(UUID universeUUID, JsonNode bodyJson, Users user) {
    String authToken = defaultUser.createAuthToken();
    if (user != null) {
      authToken = user.createAuthToken();
    }
    String method = "POST";
    String url =
        "/api/customers/"
            + defaultCustomer.getUuid()
            + "/universes/"
            + universeUUID
            + "/backups/restore";
    return doRequestWithAuthTokenAndBody(method, url, authToken, bodyJson);
  }

  private Result restoreBackupYb(JsonNode bodyJson, Users user) {
    String authToken = defaultUser.createAuthToken();
    if (user != null) {
      authToken = user.createAuthToken();
    }
    String method = "POST";
    String url = "/api/customers/" + defaultCustomer.getUuid() + "/restore";
    return doRequestWithAuthTokenAndBody(method, url, authToken, bodyJson);
  }

  private Result deleteBackup(ObjectNode bodyJson, Users user) {
    String authToken = user == null ? defaultUser.createAuthToken() : user.createAuthToken();
    String method = "DELETE";
    String url = "/api/customers/" + defaultCustomer.getUuid() + "/backups";
    return doRequestWithAuthTokenAndBody(method, url, authToken, bodyJson);
  }

  private Result deleteBackupYb(ObjectNode bodyJson, Users user) {
    String authToken = user == null ? defaultUser.createAuthToken() : user.createAuthToken();
    String method = "POST";
    String url = "/api/customers/" + defaultCustomer.getUuid() + "/backups/delete";
    return doRequestWithAuthTokenAndBody(method, url, authToken, bodyJson);
  }

  private Result createBackupYb(ObjectNode bodyJson, Users user) {
    String authToken = user == null ? defaultUser.createAuthToken() : user.createAuthToken();
    String method = "POST";
    String url = "/api/customers/" + defaultCustomer.getUuid() + "/backups";
    return doRequestWithAuthTokenAndBody(method, url, authToken, bodyJson);
  }

  private Result createBackupSchedule(ObjectNode bodyJson, Users user) {
    String authToken = user == null ? defaultUser.createAuthToken() : user.createAuthToken();
    String method = "POST";
    String url = "/api/customers/" + defaultCustomer.getUuid() + "/create_backup_schedule";
    return doRequestWithAuthTokenAndBody(method, url, authToken, bodyJson);
  }

  private Result createBackupScheduleAsync(ObjectNode bodyJson, Users user) {
    String authToken = user == null ? defaultUser.createAuthToken() : user.createAuthToken();
    String method = "POST";
    String url = "/api/customers/" + defaultCustomer.getUuid() + "/create_backup_schedule_async";
    return doRequestWithAuthTokenAndBody(method, url, authToken, bodyJson);
  }

  private Result stopBackup(Users user, UUID backupUUID) {
    String authToken = user == null ? defaultUser.createAuthToken() : user.createAuthToken();
    String method = "POST";
    String url = "/api/customers/" + defaultCustomer.getUuid() + "/backups/" + backupUUID + "/stop";
    return doRequestWithAuthToken(method, url, authToken);
  }

  private Result editBackup(Users user, ObjectNode bodyJson, UUID backupUUID) {
    String authToken = user == null ? defaultUser.createAuthToken() : user.createAuthToken();
    String method = "PUT";
    String url = "/api/customers/" + defaultCustomer.getUuid() + "/backups/" + backupUUID;
    return doRequestWithAuthTokenAndBody(method, url, authToken, bodyJson);
  }

  @Test
  public void testRestoreBackupWithInvalidUniverseUUID() {
    UUID universeUUID = UUID.randomUUID();
    JsonNode bodyJson = Json.newObject();
    when(mockBackupHelper.createRestoreTask(any(), any())).thenCallRealMethod();

    Result result = assertPlatformException(() -> restoreBackup(universeUUID, bodyJson, null));
    assertEquals(BAD_REQUEST, result.status());
    JsonNode resultJson = Json.parse(contentAsString(result));
    assertValue(resultJson, "error", "Cannot find universe " + universeUUID);
    assertAuditEntry(0, defaultCustomer.getUuid());
  }

  @Test
  public void testRestoreBackupWithInvalidParams() {
    BackupTableParams bp = new BackupTableParams();
    bp.storageConfigUUID = UUID.randomUUID();
    bp.setUniverseUUID(UUID.randomUUID());
    when(mockBackupHelper.createRestoreTask(any(), any())).thenCallRealMethod();

    Backup.create(defaultCustomer.getUuid(), bp);
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("actionType", "RESTORE");
    Result result =
        assertPlatformException(
            () -> restoreBackup(defaultUniverse.getUniverseUUID(), bodyJson, null));
    assertEquals(BAD_REQUEST, result.status());
    JsonNode resultJson = Json.parse(contentAsString(result));
    assertErrorNodeValue(resultJson, "storageConfigUUID", "This field is required");
    assertAuditEntry(0, defaultCustomer.getUuid());
  }

  @Test
  public void testRestoreBackupWithoutStorageLocation() {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST2");
    BackupTableParams bp = new BackupTableParams();
    bp.storageConfigUUID = customerConfig.getConfigUUID();
    when(mockBackupHelper.createRestoreTask(any(), any())).thenCallRealMethod();

    bp.setUniverseUUID(UUID.randomUUID());
    Backup.create(defaultCustomer.getUuid(), bp);
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("keyspace", "mock_ks");
    bodyJson.put("tableName", "mock_table");
    bodyJson.put("actionType", "RESTORE");
    bodyJson.put("storageConfigUUID", bp.storageConfigUUID.toString());
    Result result =
        assertPlatformException(
            () -> restoreBackup(defaultUniverse.getUniverseUUID(), bodyJson, null));
    assertEquals(BAD_REQUEST, result.status());
    JsonNode resultJson = Json.parse(contentAsString(result));
    assertValue(resultJson, "error", "Storage Location is required");
    assertAuditEntry(0, defaultCustomer.getUuid());
  }

  @Test
  public void testRestoreBackupWithInvalidStorageUUID() {
    BackupTableParams bp = new BackupTableParams();
    bp.storageConfigUUID = UUID.randomUUID();
    bp.setUniverseUUID(UUID.randomUUID());
    when(mockBackupHelper.createRestoreTask(any(), any())).thenCallRealMethod();
    Backup b = Backup.create(defaultCustomer.getUuid(), bp);
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("keyspace", "mock_ks");
    bodyJson.put("tableName", "mock_table");
    bodyJson.put("actionType", "RESTORE");
    bodyJson.put("storageConfigUUID", bp.storageConfigUUID.toString());
    bodyJson.put("storageLocation", b.getBackupInfo().storageLocation);
    Result result =
        assertPlatformException(
            () -> restoreBackup(defaultUniverse.getUniverseUUID(), bodyJson, null));
    assertEquals(BAD_REQUEST, result.status());
    JsonNode resultJson = Json.parse(contentAsString(result));
    assertValue(resultJson, "error", "Invalid StorageConfig UUID: " + bp.storageConfigUUID);
    assertAuditEntry(0, defaultCustomer.getUuid());
  }

  @Test
  public void testRestoreBackupWithReadOnlyUser() {
    Users user = ModelFactory.testUser(defaultCustomer, "tc@test.com", Users.Role.ReadOnly);
    BackupTableParams bp = new BackupTableParams();
    when(mockBackupHelper.createRestoreTask(any(), any())).thenCallRealMethod();
    bp.storageConfigUUID = UUID.randomUUID();
    bp.setUniverseUUID(UUID.randomUUID());
    Backup b = Backup.create(defaultCustomer.getUuid(), bp);
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("keyspace", "mock_ks");
    bodyJson.put("tableName", "mock_table");
    bodyJson.put("actionType", "RESTORE");
    bodyJson.put("storageConfigUUID", bp.storageConfigUUID.toString());
    bodyJson.put("storageLocation", b.getBackupInfo().storageLocation);
    Result result = restoreBackup(defaultUniverse.getUniverseUUID(), bodyJson, user);
    assertEquals(FORBIDDEN, result.status());
    assertEquals("User doesn't have access", contentAsString(result));
    assertAuditEntry(0, defaultCustomer.getUuid());
  }

  @Test
  public void testRestoreBackupWithValidParams() {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST3");
    BackupTableParams bp = new BackupTableParams();
    bp.storageConfigUUID = customerConfig.getConfigUUID();
    bp.setUniverseUUID(defaultUniverse.getUniverseUUID());
    Backup b = Backup.create(defaultCustomer.getUuid(), bp);
    ObjectNode bodyJson = Json.newObject();
    when(mockBackupHelper.createRestoreTask(any(), any())).thenCallRealMethod();

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

    Result result = restoreBackup(defaultUniverse.getUniverseUUID(), bodyJson, null);
    verify(mockCommissioner, times(1)).submit(taskType.capture(), taskParams.capture());
    assertEquals(TaskType.BackupUniverse, taskType.getValue());
    assertOk(result);
    JsonNode resultJson = Json.parse(contentAsString(result));
    assertValue(resultJson, "taskUUID", fakeTaskUUID.toString());
    CustomerTask ct = CustomerTask.findByTaskUUID(fakeTaskUUID);
    assertNotNull(ct);
    assertEquals(CustomerTask.TaskType.Restore, ct.getType());
    assertAuditEntry(1, defaultCustomer.getUuid());
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
    when(mockBackupHelper.createRestoreTask(any(), any())).thenCallRealMethod();

    bp.storageConfigUUID = customerConfig.getConfigUUID();
    bp.setUniverseUUID(UUID.randomUUID());
    Backup.create(defaultCustomer.getUuid(), bp);
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
    Result result = restoreBackup(defaultUniverse.getUniverseUUID(), bodyJson, null);
    assertEquals(413, result.status());
    verify(mockCommissioner, never()).submit(any(), any());
  }

  @Test
  public void testRestoreBackupWithInvalidOwner() {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST5");
    BackupTableParams bp = new BackupTableParams();
    when(mockBackupHelper.createRestoreTask(any(), any())).thenCallRealMethod();
    when(mockBackupHelper.getValidOwnerRegex()).thenCallRealMethod();

    bp.storageConfigUUID = customerConfig.getConfigUUID();
    bp.setUniverseUUID(defaultUniverse.getUniverseUUID());
    Backup.create(defaultCustomer.getUuid(), bp);
    ObjectNode bodyJson = Json.newObject();

    bodyJson.put("keyspace", "keyspace");
    bodyJson.put("actionType", "RESTORE");
    bodyJson.put("storageConfigUUID", bp.storageConfigUUID.toString());
    bodyJson.put("storageLocation", "s3://foo/bar");
    bodyJson.put("newOwner", "asgjf;jdsnc");

    Result result =
        assertPlatformException(
            () -> restoreBackup(defaultUniverse.getUniverseUUID(), bodyJson, null));
    assertEquals(BAD_REQUEST, result.status());
    verify(mockCommissioner, never()).submit(any(), any());

    bodyJson.put("newOwner", "$jdsnc");
    result =
        assertPlatformException(
            () -> restoreBackup(defaultUniverse.getUniverseUUID(), bodyJson, null));
    assertEquals(BAD_REQUEST, result.status());
    verify(mockCommissioner, never()).submit(any(), any());

    bodyJson.put("newOwner", "jdsn$c");
    result =
        assertPlatformException(
            () -> restoreBackup(defaultUniverse.getUniverseUUID(), bodyJson, null));
    assertEquals(BAD_REQUEST, result.status());
    verify(mockCommissioner, never()).submit(any(), any());

    bodyJson.put("newOwner", "jdsnc*");
    result =
        assertPlatformException(
            () -> restoreBackup(defaultUniverse.getUniverseUUID(), bodyJson, null));
    assertEquals(BAD_REQUEST, result.status());
    verify(mockCommissioner, never()).submit(any(), any());

    bodyJson.put("newOwner", "&");
    result =
        assertPlatformException(
            () -> restoreBackup(defaultUniverse.getUniverseUUID(), bodyJson, null));
    assertEquals(BAD_REQUEST, result.status());
    verify(mockCommissioner, never()).submit(any(), any());

    bodyJson.put("newOwner", "sjdachk|dkjsbfc");
    result =
        assertPlatformException(
            () -> restoreBackup(defaultUniverse.getUniverseUUID(), bodyJson, null));
    assertEquals(BAD_REQUEST, result.status());
    verify(mockCommissioner, never()).submit(any(), any());

    bodyJson.put("newOwner", "sjdachk dkjsbfc");
    result =
        assertPlatformException(
            () -> restoreBackup(defaultUniverse.getUniverseUUID(), bodyJson, null));
    assertEquals(BAD_REQUEST, result.status());
    verify(mockCommissioner, never()).submit(any(), any());

    bodyJson.put("newOwner", "sjdachk\ndkjsbfc");
    result =
        assertPlatformException(
            () -> restoreBackup(defaultUniverse.getUniverseUUID(), bodyJson, null));
    assertEquals(BAD_REQUEST, result.status());
    verify(mockCommissioner, never()).submit(any(), any());

    bodyJson.put("newOwner", "sjdachk\tdkjsbfc");
    result =
        assertPlatformException(
            () -> restoreBackup(defaultUniverse.getUniverseUUID(), bodyJson, null));
    assertEquals(BAD_REQUEST, result.status());
    verify(mockCommissioner, never()).submit(any(), any());

    bodyJson.put("newOwner", "sjdachk\tdkjsbfc");
    result =
        assertPlatformException(
            () -> restoreBackup(defaultUniverse.getUniverseUUID(), bodyJson, null));
    assertEquals(BAD_REQUEST, result.status());
    verify(mockCommissioner, never()).submit(any(), any());

    ArgumentCaptor<TaskType> taskType = ArgumentCaptor.forClass(TaskType.class);
    ArgumentCaptor<BackupTableParams> taskParams = ArgumentCaptor.forClass(BackupTableParams.class);

    bodyJson.put("newOwner", "yugabyte");
    result = restoreBackup(defaultUniverse.getUniverseUUID(), bodyJson, null);
    verify(mockCommissioner, times(1)).submit(taskType.capture(), taskParams.capture());
    assertEquals(TaskType.BackupUniverse, taskType.getValue());
    assertOk(result);
    JsonNode resultJson = Json.parse(contentAsString(result));
    assertValue(resultJson, "taskUUID", fakeTaskUUID.toString());
    CustomerTask ct = CustomerTask.findByTaskUUID(fakeTaskUUID);
    assertNotNull(ct);
    assertEquals(CustomerTask.TaskType.Restore, ct.getType());
    assertAuditEntry(1, defaultCustomer.getUuid());
  }

  @Test
  public void testDeleteBackup() {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST6");
    BackupTableParams bp = new BackupTableParams();
    bp.storageConfigUUID = customerConfig.getConfigUUID();
    bp.setUniverseUUID(defaultUniverse.getUniverseUUID());
    Backup backup = Backup.create(defaultCustomer.getUuid(), bp);
    backup.transitionState(BackupState.Completed);
    List<String> backupUUIDList = new ArrayList<>();
    backupUUIDList.add(backup.getBackupUUID().toString());
    ObjectNode resultNode = Json.newObject();
    when(mockTaskManager.isDuplicateDeleteBackupTask(
            defaultCustomer.getUuid(), backup.getBackupUUID()))
        .thenReturn(false);
    ArrayNode arrayNode = resultNode.putArray("backupUUID");
    for (String item : backupUUIDList) {
      arrayNode.add(item);
    }
    Result result = deleteBackup(resultNode, null);
    assertEquals(200, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    CustomerTask customerTask = CustomerTask.findByTaskUUID(fakeTaskUUID);
    assertEquals(customerTask.getTargetUUID(), backup.getBackupInfo().getUniverseUUID());
    assertEquals(json.get("taskUUID").size(), 1);
    assertAuditEntry(1, defaultCustomer.getUuid());
  }

  @Test
  public void testDeleteFailedBackup() {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST6");
    BackupTableParams bp = new BackupTableParams();
    bp.storageConfigUUID = customerConfig.getConfigUUID();
    bp.setUniverseUUID(defaultUniverse.getUniverseUUID());
    Backup backup = Backup.create(defaultCustomer.getUuid(), bp);
    backup.transitionState(BackupState.Failed);
    List<String> backupUUIDList = new ArrayList<>();
    backupUUIDList.add(backup.getBackupUUID().toString());
    ObjectNode resultNode = Json.newObject();
    when(mockTaskManager.isDuplicateDeleteBackupTask(
            defaultCustomer.getUuid(), backup.getBackupUUID()))
        .thenReturn(false);
    ArrayNode arrayNode = resultNode.putArray("backupUUID");
    for (String item : backupUUIDList) {
      arrayNode.add(item);
    }
    Result result = deleteBackup(resultNode, null);
    assertEquals(200, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    CustomerTask customerTask = CustomerTask.findByTaskUUID(fakeTaskUUID);
    assertEquals(customerTask.getTargetUUID(), backup.getBackupInfo().getUniverseUUID());
    assertEquals(json.get("taskUUID").size(), 1);
    assertAuditEntry(1, defaultCustomer.getUuid());
  }

  @Test
  @Parameters({"Failed", "Skipped", "FailedToDelete", "Stopped", "Completed"})
  @TestCaseName("testDeleteBackupYbWithValidStateWhenState:{0} ")
  public void testDeleteBackupYbWithValidState(BackupState state) {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST6");
    BackupTableParams bp = new BackupTableParams();
    ReflectionTestUtils.setField(mockBackupHelper, "commissioner", mockCommissioner);
    when(mockBackupHelper.createDeleteBackupTasks(any(), any())).thenCallRealMethod();
    bp.storageConfigUUID = customerConfig.getConfigUUID();
    bp.setUniverseUUID(UUID.randomUUID());
    Backup backup = Backup.create(defaultCustomer.getUuid(), bp);
    backup.transitionState(state);
    List<String> backupUUIDList = new ArrayList<>();
    backupUUIDList.add(backup.getBackupUUID().toString());
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
    assertEquals(customerTask.getTargetUUID(), backup.getUniverseUUID());
    assertEquals(json.get("taskUUID").size(), 1);
    assertAuditEntry(1, defaultCustomer.getUuid());
  }

  @Test
  @Parameters({"InProgress", "DeleteInProgress", "QueuedForDeletion"})
  @TestCaseName("testDeleteBackupYbWithInvalidStateWhenState:{0}")
  public void testDeleteBackupYbWithInvalidState(BackupState state) {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST6");
    BackupTableParams bp = new BackupTableParams();
    when(mockBackupHelper.createDeleteBackupTasks(any(), any())).thenCallRealMethod();
    bp.storageConfigUUID = customerConfig.getConfigUUID();
    bp.setUniverseUUID(UUID.randomUUID());
    Backup backup = Backup.create(defaultCustomer.getUuid(), bp);
    backup.transitionState(state);
    List<String> backupUUIDList = new ArrayList<>();
    backupUUIDList.add(backup.getBackupUUID().toString());
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
    ReflectionTestUtils.setField(mockBackupHelper, "commissioner", mockCommissioner);
    when(mockBackupHelper.createDeleteBackupTasks(any(), any())).thenCallRealMethod();
    bp.setUniverseUUID(UUID.randomUUID());
    Backup backup = Backup.create(defaultCustomer.getUuid(), bp);
    backup.transitionState(BackupState.Completed);
    List<String> backupUUIDList = new ArrayList<>();
    backupUUIDList.add(backup.getBackupUUID().toString());
    ObjectNode resultNode = Json.newObject();
    ArrayNode arrayNode = resultNode.putArray("backups");
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST6");
    for (String item : backupUUIDList) {
      ObjectNode deleteBackupObject = Json.newObject();
      deleteBackupObject.put("backupUUID", item);
      deleteBackupObject.put("storageConfigUUID", customerConfig.getConfigUUID().toString());
      arrayNode.add(deleteBackupObject);
    }
    Result result = deleteBackupYb(resultNode, null);
    assertEquals(200, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    CustomerTask customerTask = CustomerTask.findByTaskUUID(fakeTaskUUID);
    assertEquals(customerTask.getTargetUUID(), backup.getUniverseUUID());
    assertEquals(json.get("taskUUID").size(), 1);
    assertAuditEntry(1, defaultCustomer.getUuid());
    backup = Backup.getOrBadRequest(defaultCustomer.getUuid(), backup.getBackupUUID());
    assertEquals(customerConfig.getConfigUUID(), backup.getBackupInfo().storageConfigUUID);
  }

  @Test
  public void testDeleteBackupDuplicateTask() {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST600");
    BackupTableParams bp = new BackupTableParams();
    bp.storageConfigUUID = customerConfig.getConfigUUID();
    bp.setUniverseUUID(defaultUniverse.getUniverseUUID());
    Backup backup = Backup.create(defaultCustomer.getUuid(), bp);
    backup.transitionState(BackupState.Completed);
    when(mockBackupHelper.createDeleteBackupTasks(any(), any())).thenCallRealMethod();

    List<String> backupUUIDList = new ArrayList<>();
    backupUUIDList.add(backup.getBackupUUID().toString());
    ObjectNode resultNode = Json.newObject();
    when(mockTaskManager.isDuplicateDeleteBackupTask(
            defaultCustomer.getUuid(), backup.getBackupUUID()))
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
    Util.setPID(defaultBackup.getBackupUUID(), process);

    taskInfo = new TaskInfo(TaskType.CreateTable, null);
    taskInfo.setDetails(Json.newObject());
    taskInfo.setOwner("");
    taskInfo.setTaskUUID(taskUUID);
    taskInfo.save();

    defaultBackup.setTaskUUID(taskUUID);
    defaultBackup.save();
    ExecutorService executorService = Executors.newSingleThreadExecutor();

    Callable<Result> callable =
        () -> {
          return stopBackup(null, defaultBackup.getBackupUUID());
        };
    Future<Result> future = executorService.submit(callable);
    Thread.sleep(1000);
    taskInfo.setTaskState(State.Failure);
    taskInfo.save();

    Result result = future.get();
    executorService.shutdown();
    assertEquals(200, result.status());
    assertAuditEntry(1, defaultCustomer.getUuid());
  }

  @Test
  public void testStopBackupWithPermissions()
      throws IOException, InterruptedException, ExecutionException {

    RuntimeConfigEntry.upsertGlobal("yb.rbac.use_new_authz", "true");
    ResourceGroup rG = new ResourceGroup(new HashSet<>(Arrays.asList(rd1)));
    RoleBinding.create(defaultUser, RoleBindingType.Custom, role, rG);

    ProcessBuilder processBuilderObject = new ProcessBuilder("test");
    Process process = processBuilderObject.start();
    Util.setPID(defaultBackup.getBackupUUID(), process);

    taskInfo = new TaskInfo(TaskType.CreateTable, null);
    taskInfo.setDetails(Json.newObject());
    taskInfo.setOwner("");
    taskInfo.setTaskUUID(taskUUID);
    taskInfo.save();

    defaultBackup.setTaskUUID(taskUUID);
    defaultBackup.save();
    ExecutorService executorService = Executors.newSingleThreadExecutor();

    Callable<Result> callable =
        () -> {
          return stopBackup(null, defaultBackup.getBackupUUID());
        };
    Future<Result> future = executorService.submit(callable);
    Thread.sleep(1000);
    taskInfo.setTaskState(State.Failure);
    taskInfo.save();

    Result result = future.get();
    executorService.shutdown();
    assertEquals(200, result.status());
    assertAuditEntry(1, defaultCustomer.getUuid());
  }

  @Test
  public void testStopBackupCompleted() {
    when(mockBackupHelper.stopBackup(any(), any())).thenCallRealMethod();

    defaultBackup.transitionState(BackupState.Completed);
    Result result =
        assertThrows(
                PlatformServiceException.class,
                () -> stopBackup(null, defaultBackup.getBackupUUID()))
            .buildResult(fakeRequest);
    assertEquals(400, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(json.get("error").asText(), "The process you want to stop is not in progress.");
  }

  @Test
  public void testStopBackupMaxRetry() throws IOException {
    ProcessBuilder processBuilderObject = new ProcessBuilder("test");
    Process process = processBuilderObject.start();
    Util.setPID(defaultBackup.getBackupUUID(), process);
    when(mockBackupHelper.stopBackup(any(), any())).thenCallRealMethod();
    taskInfo = new TaskInfo(TaskType.CreateTable, null);
    taskInfo.setDetails(Json.newObject());
    taskInfo.setOwner("");
    taskInfo.setTaskUUID(taskUUID);
    taskInfo.save();

    defaultBackup.setTaskUUID(taskUUID);
    defaultBackup.save();
    Result result =
        assertThrows(
                PlatformServiceException.class,
                () -> stopBackup(null, defaultBackup.getBackupUUID()))
            .buildResult(fakeRequest);
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
        assertPlatformException(
            () -> editBackup(defaultUser, bodyJson, defaultBackup.getBackupUUID()));
    assertBadRequest(result, "Cannot edit a backup that is in progress state");
  }

  @Test
  public void testEditBackupWithNegativeDeletionTime() {
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("timeBeforeDeleteFromPresentInMillis", -1L);

    Result result =
        assertPlatformException(
            () -> editBackup(defaultUser, bodyJson, defaultBackup.getBackupUUID()));
    assertEquals(BAD_REQUEST, result.status());
    assertBadRequest(
        result,
        "Please provide either a non negative expiry time or storage config to edit backup");
  }

  @Test
  public void testEditBackupNeverExpire() {
    defaultBackup.setState(BackupState.Completed);
    defaultBackup.update();
    Backup backup =
        Backup.getOrBadRequest(defaultCustomer.getUuid(), defaultBackup.getBackupUUID());

    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("timeBeforeDeleteFromPresentInMillis", 0L);
    Result result = editBackup(defaultUser, bodyJson, defaultBackup.getBackupUUID());
    backup = Backup.getOrBadRequest(defaultCustomer.getUuid(), defaultBackup.getBackupUUID());

    assert (backup.getExpiry() == null);
  }

  @Test
  public void testEditBackup() {
    defaultBackup.setState(BackupState.Completed);
    defaultBackup.update();
    Backup backup =
        Backup.getOrBadRequest(defaultCustomer.getUuid(), defaultBackup.getBackupUUID());
    // assertTrue(backup.state.equals(BackupState.Completed));

    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("timeBeforeDeleteFromPresentInMillis", 86400000L);
    bodyJson.put("expiryTimeUnit", "DAYS");
    Result result = editBackup(defaultUser, bodyJson, defaultBackup.getBackupUUID());
    backup = Backup.getOrBadRequest(defaultCustomer.getUuid(), defaultBackup.getBackupUUID());
    long afterTimeInMillis = System.currentTimeMillis() + 86400000L;
    long beforeTimeInMillis = System.currentTimeMillis() + 85400000L;

    long expiryTimeInMillis = backup.getExpiry().getTime();
    assertTrue(expiryTimeInMillis > beforeTimeInMillis);
    assertTrue(afterTimeInMillis > expiryTimeInMillis);
  }

  @Test
  public void testEditBackupWithoutTimeUnit() {

    defaultBackup.setState(BackupState.Completed);
    defaultBackup.update();
    Backup backup =
        Backup.getOrBadRequest(defaultCustomer.getUuid(), defaultBackup.getBackupUUID());
    // assertTrue(backup.state.equals(BackupState.Completed));

    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("timeBeforeDeleteFromPresentInMillis", 86400000L);
    Result result =
        assertPlatformException(
            () -> editBackup(defaultUser, bodyJson, defaultBackup.getBackupUUID()));
    assertBadRequest(result, "Please provide a time unit for backup expiry");
  }

  @Test
  public void testEditBackupWithIncrementalBackup() {
    defaultBackup.setBaseBackupUUID(UUID.randomUUID());
    defaultBackup.setState(BackupState.Completed);
    defaultBackup.update();
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("timeBeforeDeleteFromPresentInMillis", 86400000L);
    bodyJson.put("expiryTimeUnit", "DAYS");
    Result result =
        assertPlatformException(
            () -> editBackup(defaultUser, bodyJson, defaultBackup.getBackupUUID()));
    assertEquals(BAD_REQUEST, result.status());
    assertBadRequest(result, "Cannot edit an incremental backup");
  }

  @Test
  public void testEditStorageConfigSuccess() {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST7");
    BackupTableParams bp = new BackupTableParams();
    bp.storageConfigUUID = customerConfig.getConfigUUID();
    bp.setUniverseUUID(UUID.randomUUID());
    Backup backup = Backup.create(defaultCustomer.getUuid(), bp);
    backup.transitionState(BackupState.Completed);
    UUID invalidConfigUUID = UUID.randomUUID();
    backup.updateStorageConfigUUID(invalidConfigUUID);
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("storageConfigUUID", customerConfig.getConfigUUID().toString());
    bodyJson.put("timeBeforeDeleteFromPresentInMillis", "-1");
    Result result = editBackup(defaultUser, bodyJson, backup.getBackupUUID());
    assertOk(result);
    assertAuditEntry(1, defaultCustomer.getUuid());
    backup.refresh();
    assertEquals(customerConfig.getConfigUUID(), backup.getStorageConfigUUID());
    assertEquals(customerConfig.getConfigUUID(), backup.getBackupInfo().storageConfigUUID);
  }

  @Test
  public void testEditStorageConfigWithAlreadyActiveConfig() {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST8");
    BackupTableParams bp = new BackupTableParams();
    bp.storageConfigUUID = customerConfig.getConfigUUID();
    bp.setUniverseUUID(UUID.randomUUID());
    Backup backup = Backup.create(defaultCustomer.getUuid(), bp);
    backup.transitionState(BackupState.Completed);
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("storageConfigUUID", customerConfig.getConfigUUID().toString());
    Result result =
        assertPlatformException(() -> editBackup(defaultUser, bodyJson, backup.getBackupUUID()));
    assertBadRequest(result, "Active storage config is already assigned to the backup");
    assertAuditEntry(0, defaultCustomer.getUuid());
    backup.refresh();
    assertEquals(customerConfig.getConfigUUID(), backup.getStorageConfigUUID());
    assertEquals(customerConfig.getConfigUUID(), backup.getBackupInfo().storageConfigUUID);
  }

  @Test
  @Parameters({"InProgress", "DeleteInProgress", "QueuedForDeletion"})
  @TestCaseName("testEditStorageConfigWithBackupInState:{0} ")
  public void testEditStorageConfigWithBackpInProgressState(BackupState state) {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST9");
    BackupTableParams bp = new BackupTableParams();
    bp.storageConfigUUID = customerConfig.getConfigUUID();
    UUID invalidConfigUUID = UUID.randomUUID();
    bp.setUniverseUUID(UUID.randomUUID());
    Backup backup = Backup.create(defaultCustomer.getUuid(), bp);
    backup.transitionState(state);
    backup.updateStorageConfigUUID(invalidConfigUUID);
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("storageConfigUUID", customerConfig.getConfigUUID().toString());
    Result result =
        assertPlatformException(() -> editBackup(defaultUser, bodyJson, backup.getBackupUUID()));
    assertBadRequest(result, "Cannot edit a backup that is in progress state");
    assertAuditEntry(0, defaultCustomer.getUuid());
    backup.refresh();
    assertEquals(invalidConfigUUID, backup.getStorageConfigUUID());
    assertEquals(invalidConfigUUID, backup.getBackupInfo().storageConfigUUID);
  }

  @Test
  public void testEditStorageConfigWithQueuedForDeletionConfig() {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST10");
    BackupTableParams bp = new BackupTableParams();
    bp.storageConfigUUID = customerConfig.getConfigUUID();
    UUID invalidConfigUUID = UUID.randomUUID();
    bp.setUniverseUUID(UUID.randomUUID());
    Backup backup = Backup.create(defaultCustomer.getUuid(), bp);
    backup.transitionState(BackupState.Completed);
    backup.updateStorageConfigUUID(invalidConfigUUID);
    customerConfig.updateState(ConfigState.QueuedForDeletion);
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("storageConfigUUID", customerConfig.getConfigUUID().toString());
    Result result =
        assertPlatformException(() -> editBackup(defaultUser, bodyJson, backup.getBackupUUID()));
    assertBadRequest(result, "");
    assertAuditEntry(0, defaultCustomer.getUuid());
    backup.refresh();
    assertEquals(invalidConfigUUID, backup.getStorageConfigUUID());
    assertEquals(invalidConfigUUID, backup.getBackupInfo().storageConfigUUID);
  }

  @Test
  public void testEditStorageConfigWithInvalidStorageConfigType() {
    CustomerConfig customerConfig = ModelFactory.createGcsStorageConfig(defaultCustomer, "TEST11");
    BackupTableParams bp = new BackupTableParams();
    bp.storageConfigUUID = customerConfig.getConfigUUID();
    UUID invalidConfigUUID = UUID.randomUUID();
    bp.setUniverseUUID(UUID.randomUUID());
    Backup backup = Backup.create(defaultCustomer.getUuid(), bp);
    backup.transitionState(BackupState.Completed);
    backup.updateStorageConfigUUID(invalidConfigUUID);
    CustomerConfig newConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST12");
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("storageConfigUUID", newConfig.getConfigUUID().toString());
    Result result =
        assertPlatformException(() -> editBackup(defaultUser, bodyJson, backup.getBackupUUID()));
    assertBadRequest(
        result,
        "Cannot assign "
            + newConfig.getName()
            + " type config to the backup stored in "
            + customerConfig.getName());
    assertAuditEntry(0, defaultCustomer.getUuid());
    backup.refresh();
    assertEquals(invalidConfigUUID, backup.getStorageConfigUUID());
    assertEquals(invalidConfigUUID, backup.getBackupInfo().storageConfigUUID);
  }

  @Test
  public void testEditStorageConfigWithInvalidConfigType() {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST13");
    BackupTableParams bp = new BackupTableParams();
    bp.storageConfigUUID = customerConfig.getConfigUUID();
    UUID invalidConfigUUID = UUID.randomUUID();
    bp.setUniverseUUID(UUID.randomUUID());
    Backup backup = Backup.create(defaultCustomer.getUuid(), bp);
    backup.transitionState(BackupState.Completed);
    backup.updateStorageConfigUUID(invalidConfigUUID);
    CustomerConfig newConfig = ModelFactory.setCallhomeLevel(defaultCustomer, "default");
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("storageConfigUUID", newConfig.getConfigUUID().toString());
    Result result =
        assertPlatformException(() -> editBackup(defaultUser, bodyJson, backup.getBackupUUID()));
    assertBadRequest(
        result, "Cannot assign " + newConfig.getType() + " type config in place of Storage Config");
    assertAuditEntry(0, defaultCustomer.getUuid());
    backup.refresh();
    assertEquals(invalidConfigUUID, backup.getStorageConfigUUID());
    assertEquals(invalidConfigUUID, backup.getBackupInfo().storageConfigUUID);
  }

  @Test
  public void testEditStorageConfigWithInvalidConfig() {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST14");
    BackupTableParams bp = new BackupTableParams();
    bp.storageConfigUUID = customerConfig.getConfigUUID();
    bp.setUniverseUUID(UUID.randomUUID());
    Backup backup = Backup.create(defaultCustomer.getUuid(), bp);
    backup.transitionState(BackupState.Completed);
    UUID invalidConfigUUID = UUID.randomUUID();
    backup.updateStorageConfigUUID(invalidConfigUUID);
    doThrow(
            new PlatformServiceException(
                BAD_REQUEST, "Storage config TEST14 cannot access backup locations"))
        .when(mockBackupHelper)
        .validateStorageConfigOnBackup(any(), any());
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("storageConfigUUID", customerConfig.getConfigUUID().toString());
    Result result =
        assertPlatformException(() -> editBackup(defaultUser, bodyJson, backup.getBackupUUID()));
    assertBadRequest(result, "Storage config TEST14 cannot access backup locations");
    assertAuditEntry(0, defaultCustomer.getUuid());
    backup.refresh();
    assertEquals(invalidConfigUUID, backup.getStorageConfigUUID());
    assertEquals(invalidConfigUUID, backup.getBackupInfo().storageConfigUUID);
  }

  @Test
  public void testInvalidEditBackupTaskParams() {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST14");
    BackupTableParams bp = new BackupTableParams();
    bp.storageConfigUUID = customerConfig.getConfigUUID();
    bp.setUniverseUUID(UUID.randomUUID());
    Backup backup = Backup.create(defaultCustomer.getUuid(), bp);
    backup.transitionState(BackupState.Completed);
    UUID invalidConfigUUID = UUID.randomUUID();
    backup.updateStorageConfigUUID(invalidConfigUUID);
    ObjectNode bodyJson = Json.newObject();
    bodyJson.put("timeBeforeDeleteFromPresentInMillis", "-1");
    Result result =
        assertPlatformException(() -> editBackup(defaultUser, bodyJson, backup.getBackupUUID()));
    assertBadRequest(
        result,
        "Please provide either a non negative expiry time" + " or storage config to edit backup");
    assertAuditEntry(0, defaultCustomer.getUuid());
    backup.refresh();
    assertEquals(invalidConfigUUID, backup.getStorageConfigUUID());
    assertEquals(invalidConfigUUID, backup.getBackupInfo().storageConfigUUID);
  }

  private Result setThrottleParams(ObjectNode bodyJson, UUID universeUUID) {
    String authToken = defaultUser.createAuthToken();
    String method = "POST";
    String url =
        "/api/customers/"
            + defaultCustomer.getUuid()
            + "/universes/"
            + universeUUID.toString()
            + "/ybc_throttle_params";
    return doRequestWithAuthTokenAndBody(method, url, authToken, bodyJson);
  }

  private Result getThrottleParams(UUID universeUUID) {
    String authToken = defaultUser.createAuthToken();
    String method = "GET";
    String url =
        "/api/customers/"
            + defaultCustomer.getUuid()
            + "/universes/"
            + universeUUID.toString()
            + "/ybc_throttle_params";
    return doRequestWithAuthToken(method, url, authToken);
  }

  @Test
  public void testSetThrottleParamsSuccess() {
    Universe universe = ModelFactory.createUniverse("TEST1", defaultCustomer.getUuid());
    UniverseDefinitionTaskParams details = universe.getUniverseDetails();
    ObjectNode bodyJson = Json.newObject();
    details.setYbcInstalled(true);
    universe.setUniverseDetails(details);
    universe.save();
    doNothing().when(mockYbcManager).setThrottleParams(any(), any());
    Result result = setThrottleParams(bodyJson, universe.getUniverseUUID());
    assertOk(result);
  }

  @Test
  public void testSetThrottleParamsFailedUniverseLocked() {
    Universe universe = ModelFactory.createUniverse("TEST2", defaultCustomer.getUuid());
    UniverseDefinitionTaskParams details = universe.getUniverseDetails();
    ObjectNode bodyJson = Json.newObject();
    details.setYbcInstalled(true);
    details.updateInProgress = true;
    universe.setUniverseDetails(details);
    universe.save();
    Result result =
        assertPlatformException(() -> setThrottleParams(bodyJson, universe.getUniverseUUID()));
    assertBadRequest(result, "Cannot set throttle params, universe task in progress.");
  }

  @Test
  public void testSetThrottleParamsFailedUniversePaused() {
    Universe universe = ModelFactory.createUniverse("TEST3", defaultCustomer.getUuid());
    UniverseDefinitionTaskParams details = universe.getUniverseDetails();
    ObjectNode bodyJson = Json.newObject();
    details.setYbcInstalled(true);
    details.universePaused = true;
    universe.setUniverseDetails(details);
    universe.save();
    Result result =
        assertPlatformException(() -> setThrottleParams(bodyJson, universe.getUniverseUUID()));
    assertBadRequest(result, "Cannot set throttle params, universe is paused.");
  }

  @Test
  public void testSetThrottleParamsFailedBackupInProgress() {
    Universe universe = ModelFactory.createUniverse("TEST4", defaultCustomer.getUuid());
    UniverseDefinitionTaskParams details = universe.getUniverseDetails();
    details.setYbcInstalled(true);
    ObjectNode bodyJson = Json.newObject();
    details.updateInProgress = true;
    universe.setUniverseDetails(details);
    universe.save();
    Result result =
        assertPlatformException(() -> setThrottleParams(bodyJson, universe.getUniverseUUID()));
    assertBadRequest(result, "Cannot set throttle params, universe task in progress.");
  }

  @Test
  public void testSetThrottleParamsTaskFailed() {
    Universe universe = ModelFactory.createUniverse("TEST5", defaultCustomer.getUuid());
    UniverseDefinitionTaskParams details = universe.getUniverseDetails();
    details.setYbcInstalled(true);
    ObjectNode bodyJson = Json.newObject();
    universe.setUniverseDetails(details);
    universe.save();
    doThrow(new RuntimeException("some failure"))
        .when(mockYbcManager)
        .setThrottleParams(any(), any());
    Result result =
        assertPlatformException(() -> setThrottleParams(bodyJson, universe.getUniverseUUID()));
    assertInternalServerError(
        result,
        String.format(
            "Got error setting throttle params for universe %s, error: %s",
            universe.getUniverseUUID().toString(), "some failure"));
  }

  @Test
  public void testGetThrottleParamsSuccess() {
    Universe universe = ModelFactory.createUniverse("TEST6", defaultCustomer.getUuid());
    UniverseDefinitionTaskParams details = universe.getUniverseDetails();
    details.setYbcInstalled(true);
    universe.setUniverseDetails(details);
    universe.save();
    YbcThrottleParametersResponse response = new YbcThrottleParametersResponse();
    when(mockYbcManager.getThrottleParams(any())).thenReturn(response);
    Result result = getThrottleParams(universe.getUniverseUUID());
    assertOk(result);
  }

  @Test
  public void testGetThrottleParamsFailedUniversePaused() {
    Universe universe = ModelFactory.createUniverse("TEST7", defaultCustomer.getUuid());
    UniverseDefinitionTaskParams details = universe.getUniverseDetails();
    details.setYbcInstalled(true);
    details.universePaused = true;
    universe.setUniverseDetails(details);
    universe.save();
    Result result = assertPlatformException(() -> getThrottleParams(universe.getUniverseUUID()));
    assertBadRequest(result, "Cannot get throttle params, universe is paused.");
  }

  @Test
  public void testGetThrottleParamsTaskFailed() {
    Universe universe = ModelFactory.createUniverse("TEST8", defaultCustomer.getUuid());
    UniverseDefinitionTaskParams details = universe.getUniverseDetails();
    details.setYbcInstalled(true);
    universe.setUniverseDetails(details);
    universe.save();
    doThrow(new RuntimeException("some failure")).when(mockYbcManager).getThrottleParams(any());
    Result result = assertPlatformException(() -> getThrottleParams(universe.getUniverseUUID()));
    assertInternalServerError(
        result,
        String.format(
            "Got error getting throttle params for universe %s, error: %s",
            universe.getUniverseUUID().toString(), "some failure"));
  }

  @Test
  public void testDeleteFailedIncrementalBackup() {
    BackupTableParams bp = new BackupTableParams();
    bp.storageConfigUUID = customerConfig.getConfigUUID();
    bp.setUniverseUUID(UUID.randomUUID());
    bp.baseBackupUUID = UUID.randomUUID();
    Backup backup = Backup.create(defaultCustomer.getUuid(), bp);
    backup.transitionState(BackupState.Failed);
    List<String> backupUUIDList = new ArrayList<>();
    backupUUIDList.add(backup.getBackupUUID().toString());
    ReflectionTestUtils.setField(mockBackupHelper, "commissioner", mockCommissioner);
    when(mockBackupHelper.createDeleteBackupTasks(any(), any())).thenCallRealMethod();
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
    assertEquals(customerTask.getTargetUUID(), backup.getUniverseUUID());
    assertEquals(json.get("taskUUID").size(), 1);
    assertAuditEntry(1, defaultCustomer.getUuid());
  }

  @Test
  public void testDeleteCompletedIncrementalBackup() {
    BackupTableParams bp = new BackupTableParams();
    bp.storageConfigUUID = customerConfig.getConfigUUID();
    bp.setUniverseUUID(UUID.randomUUID());
    bp.baseBackupUUID = UUID.randomUUID();
    Backup backup = Backup.create(defaultCustomer.getUuid(), bp);
    backup.transitionState(BackupState.Completed);
    List<String> backupUUIDList = new ArrayList<>();
    backupUUIDList.add(backup.getBackupUUID().toString());
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
    assertAuditEntry(1, defaultCustomer.getUuid());
  }

  private Result getPagedRestoreList(UUID customerUUID, JsonNode body) {
    String authToken = defaultUser.createAuthToken();
    String method = "POST";
    String url = "/api/customers/" + customerUUID + "/restore/page";
    return doRequestWithAuthTokenAndBody(method, url, authToken, body);
  }

  @Test
  public void testGetPagedRestoreList() {
    RestoreBackupParams restoreBackupParams = new RestoreBackupParams();
    restoreBackupParams.customerUUID = defaultCustomer.getUuid();
    restoreBackupParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    restoreBackupParams.storageConfigUUID = customerConfig.getConfigUUID();
    restoreBackupParams.backupStorageInfoList = new ArrayList<BackupStorageInfo>();
    BackupStorageInfo storageInfo = new BackupStorageInfo();
    storageInfo.storageLocation = defaultBackup.getBackupInfo().storageLocation;
    storageInfo.keyspace = defaultBackup.getBackupInfo().getKeyspace();
    restoreBackupParams.backupStorageInfoList.add(storageInfo);
    ObjectMapper mapper = new ObjectMapper();
    JsonNode bodyJson = mapper.valueToTree(restoreBackupParams);
    TaskInfo taskInfo = new TaskInfo(TaskType.RestoreBackup, null);
    taskInfo.setDetails(bodyJson);
    taskInfo.setOwner("");
    taskInfo.setTaskState(TaskInfo.State.Success);
    UUID taskUUID = UUID.randomUUID();
    taskInfo.setTaskUUID(taskUUID);
    taskInfo.save();

    TaskInfo taskInfoSub = new TaskInfo(TaskType.RestoreBackupYb, null);
    taskInfoSub.setDetails(bodyJson);
    taskInfoSub.setOwner("");
    UUID taskUUIDSub = UUID.randomUUID();
    taskInfoSub.setTaskUUID(taskUUIDSub);
    taskInfoSub.setParentUuid(taskUUID);
    taskInfoSub.setPosition(1);
    taskInfoSub.setTaskState(TaskInfo.State.Success);
    taskInfoSub.save();

    Restore restore = Restore.create(taskUUID, restoreBackupParams);
    restore.setState(Restore.State.Completed);
    restore.save();

    ObjectNode bodyJson3 = Json.newObject();
    bodyJson3.put("direction", "DESC");
    bodyJson3.put("sortBy", "createTime");
    bodyJson3.put("offset", 0);
    bodyJson3.set("filter", Json.newObject());
    Result result = getPagedRestoreList(defaultCustomer.getUuid(), bodyJson3);
    assertOk(result);
    JsonNode restoreJson = Json.parse(contentAsString(result));
    ArrayNode response = (ArrayNode) restoreJson.get("entities");
    assertEquals(2, response.size());
    assertAuditEntry(0, defaultCustomer.getUuid());

    bodyJson3 = Json.newObject();
    bodyJson3.put("direction", "DESC");
    bodyJson3.put("sortBy", "createTime");
    bodyJson3.put("offset", 0);
    bodyJson3.set("filter", Json.newObject().set("states", Json.newArray().add("Completed")));
    result = getPagedRestoreList(defaultCustomer.getUuid(), bodyJson3);
    assertOk(result);
    restoreJson = Json.parse(contentAsString(result));
    response = (ArrayNode) restoreJson.get("entities");
    assertEquals(1, response.size());
    assertAuditEntry(0, defaultCustomer.getUuid());

    bodyJson3 = Json.newObject();
    bodyJson3.put("direction", "DESC");
    bodyJson3.put("sortBy", "createTime");
    bodyJson3.put("offset", 0);
    bodyJson3.set("filter", Json.newObject().set("states", Json.newArray().add("Created")));
    result = getPagedRestoreList(defaultCustomer.getUuid(), bodyJson3);
    assertOk(result);
    restoreJson = Json.parse(contentAsString(result));
    response = (ArrayNode) restoreJson.get("entities");
    assertEquals(1, response.size());
    assertAuditEntry(0, defaultCustomer.getUuid());

    bodyJson3 = Json.newObject();
    bodyJson3.put("direction", "DESC");
    bodyJson3.put("sortBy", "createTime");
    bodyJson3.put("offset", 0);
    bodyJson3.set(
        "filter", Json.newObject().set("states", Json.newArray().add("Created").add("Completed")));
    result = getPagedRestoreList(defaultCustomer.getUuid(), bodyJson3);
    assertOk(result);
    restoreJson = Json.parse(contentAsString(result));
    response = (ArrayNode) restoreJson.get("entities");
    assertEquals(2, response.size());
    assertAuditEntry(0, defaultCustomer.getUuid());

    bodyJson3 = Json.newObject();
    bodyJson3.put("direction", "DESC");
    bodyJson3.put("sortBy", "createTime");
    bodyJson3.put("offset", 0);
    bodyJson3.set("filter", Json.newObject().set("states", Json.newArray().add("Failed")));
    result = getPagedRestoreList(defaultCustomer.getUuid(), bodyJson3);
    assertOk(result);
    restoreJson = Json.parse(contentAsString(result));
    response = (ArrayNode) restoreJson.get("entities");
    assertEquals(0, response.size());
    assertAuditEntry(0, defaultCustomer.getUuid());
  }

  @Test
  public void testGetPagedRestoreListWithUniversesFilter() {
    RestoreBackupParams restoreBackupParams = new RestoreBackupParams();
    restoreBackupParams.customerUUID = defaultCustomer.getUuid();
    Universe universe =
        ModelFactory.createUniverse(
            "restore-universe-1", UUID.randomUUID(), defaultCustomer.getId(), CloudType.aws);

    restoreBackupParams.setUniverseUUID(universe.getUniverseUUID());
    restoreBackupParams.storageConfigUUID = customerConfig.getConfigUUID();
    restoreBackupParams.backupStorageInfoList = new ArrayList<BackupStorageInfo>();
    BackupStorageInfo storageInfo = new BackupStorageInfo();
    storageInfo.storageLocation = defaultBackup.getBackupInfo().storageLocation;
    storageInfo.keyspace = defaultBackup.getBackupInfo().getKeyspace();
    restoreBackupParams.backupStorageInfoList.add(storageInfo);
    ObjectMapper mapper = new ObjectMapper();
    JsonNode bodyJson = mapper.valueToTree(restoreBackupParams);
    TaskInfo taskInfo = new TaskInfo(TaskType.RestoreBackup, null);
    taskInfo.setDetails(bodyJson);
    taskInfo.setOwner("");
    taskInfo.setTaskState(TaskInfo.State.Success);
    UUID taskUUID = UUID.randomUUID();
    taskInfo.setTaskUUID(taskUUID);
    taskInfo.save();

    TaskInfo taskInfoSub = new TaskInfo(TaskType.RestoreBackupYb, null);
    taskInfoSub.setDetails(bodyJson);
    taskInfoSub.setOwner("");
    UUID taskUUIDSub = UUID.randomUUID();
    taskInfoSub.setTaskUUID(taskUUIDSub);
    taskInfoSub.setParentUuid(taskUUID);
    taskInfoSub.setPosition(1);
    taskInfoSub.setTaskState(TaskInfo.State.Success);
    taskInfoSub.save();

    Restore restore = Restore.create(taskUUID, restoreBackupParams);

    ObjectNode bodyJson3 = Json.newObject();
    bodyJson3.put("direction", "DESC");
    bodyJson3.put("sortBy", "createTime");
    bodyJson3.put("offset", 0);
    bodyJson3.set(
        "filter",
        Json.newObject()
            .set("sourceUniverseNameList", Json.newArray().add(defaultUniverse.getName())));
    Result result = getPagedRestoreList(defaultCustomer.getUuid(), bodyJson3);
    assertOk(result);
    JsonNode restoreJson = Json.parse(contentAsString(result));
    ArrayNode response = (ArrayNode) restoreJson.get("entities");
    assertEquals(2, response.size());
    assertAuditEntry(0, defaultCustomer.getUuid());

    bodyJson3 = Json.newObject();
    bodyJson3.put("direction", "DESC");
    bodyJson3.put("sortBy", "createTime");
    bodyJson3.put("offset", 0);
    bodyJson3.set(
        "filter", Json.newObject().set("sourceUniverseNameList", Json.newArray().add("NewTest")));
    result = getPagedRestoreList(defaultCustomer.getUuid(), bodyJson3);
    assertOk(result);
    restoreJson = Json.parse(contentAsString(result));
    response = (ArrayNode) restoreJson.get("entities");
    assertEquals(0, response.size());
    assertAuditEntry(0, defaultCustomer.getUuid());

    bodyJson3 = Json.newObject();
    bodyJson3.put("direction", "DESC");
    bodyJson3.put("sortBy", "createTime");
    bodyJson3.put("offset", 0);
    bodyJson3.set(
        "filter",
        Json.newObject()
            .set(
                "sourceUniverseNameList",
                Json.newArray().add(defaultUniverse.getName()).add(universe.getName())));
    result = getPagedRestoreList(defaultCustomer.getUuid(), bodyJson3);
    assertOk(result);
    restoreJson = Json.parse(contentAsString(result));
    response = (ArrayNode) restoreJson.get("entities");
    assertEquals(2, response.size());
    assertAuditEntry(0, defaultCustomer.getUuid());

    bodyJson3 = Json.newObject();
    bodyJson3.put("direction", "DESC");
    bodyJson3.put("sortBy", "createTime");
    bodyJson3.put("offset", 0);
    bodyJson3.set(
        "filter",
        Json.newObject()
            .set(
                "universeUUIDList",
                Json.newArray().add(defaultUniverse.getUniverseUUID().toString())));
    result = getPagedRestoreList(defaultCustomer.getUuid(), bodyJson3);
    assertOk(result);
    restoreJson = Json.parse(contentAsString(result));
    response = (ArrayNode) restoreJson.get("entities");
    assertEquals(1, response.size());
    assertAuditEntry(0, defaultCustomer.getUuid());

    bodyJson3 = Json.newObject();
    bodyJson3.put("direction", "DESC");
    bodyJson3.put("sortBy", "createTime");
    bodyJson3.put("offset", 0);
    bodyJson3.set(
        "filter",
        Json.newObject()
            .set(
                "universeUUIDList",
                Json.newArray()
                    .add(defaultUniverse.getUniverseUUID().toString())
                    .add(universe.getUniverseUUID().toString())));
    result = getPagedRestoreList(defaultCustomer.getUuid(), bodyJson3);
    assertOk(result);
    restoreJson = Json.parse(contentAsString(result));
    response = (ArrayNode) restoreJson.get("entities");
    assertEquals(2, response.size());
    assertAuditEntry(0, defaultCustomer.getUuid());

    bodyJson3 = Json.newObject();
    bodyJson3.put("direction", "DESC");
    bodyJson3.put("sortBy", "createTime");
    bodyJson3.put("offset", 0);
    bodyJson3.set(
        "filter",
        Json.newObject()
            .set("universeUUIDList", Json.newArray().add(UUID.randomUUID().toString())));
    result = getPagedRestoreList(defaultCustomer.getUuid(), bodyJson3);
    assertOk(result);
    restoreJson = Json.parse(contentAsString(result));
    response = (ArrayNode) restoreJson.get("entities");
    assertEquals(0, response.size());
    assertAuditEntry(0, defaultCustomer.getUuid());
  }
}
