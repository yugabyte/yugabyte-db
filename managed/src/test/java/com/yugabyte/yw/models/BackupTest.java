// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import static com.yugabyte.yw.models.Backup.BackupState.Failed;
import static com.yugabyte.yw.models.Backup.BackupState.InProgress;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThat;
import static org.junit.Assert.assertThrows;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.RegexMatcher;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.models.Backup.BackupCategory;
import com.yugabyte.yw.models.Backup.BackupState;
import com.yugabyte.yw.models.Backup.BackupVersion;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.helpers.TaskType;
import io.ebean.DB;
import io.ebean.SqlUpdate;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Date;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import lombok.extern.slf4j.Slf4j;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.yb.CommonTypes.TableType;
import play.libs.Json;

@RunWith(JUnitParamsRunner.class)
@Slf4j
public class BackupTest extends FakeDBApplication {
  private Customer defaultCustomer;
  private CustomerConfig s3StorageConfig;
  private final String timestampRegex = "\\d{4}-[0-1]\\d-[0-3]\\dT[0-2]\\d:[0-5]\\d:[0-5]\\d";

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    s3StorageConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST26");
  }

  @Test
  public void testCreate() {
    UUID universeUUID = UUID.randomUUID();
    Backup b =
        ModelFactory.createBackup(
            defaultCustomer.getUuid(), universeUUID, s3StorageConfig.getConfigUUID());
    assertNotNull(b);
    String storageRegex =
        "s3://foo/univ-"
            + universeUUID
            + "/backup-[a-zA-Z0-9]+?/full/"
            + timestampRegex
            + "/table-foo.bar-[a-zA-Z0-9]*";
    assertThat(b.getBackupInfo().storageLocation, RegexMatcher.matchesRegex(storageRegex));
    assertEquals(s3StorageConfig.getConfigUUID(), b.getBackupInfo().storageConfigUUID);
    assertEquals(InProgress, b.getState());
  }

  @Test
  public void testCreateWithoutTableUUID() {
    UUID universeUUID = UUID.randomUUID();
    BackupTableParams params = new BackupTableParams();
    params.storageConfigUUID = s3StorageConfig.getConfigUUID();
    params.setUniverseUUID(universeUUID);
    params.setKeyspace("foo");
    params.setTableName("bar");
    Backup b = Backup.create(defaultCustomer.getUuid(), params);
    String storageRegex =
        "s3://foo/univ-"
            + universeUUID
            + "/backup-[a-zA-Z0-9]+?/full/"
            + timestampRegex
            + "/table-foo.bar";
    assertThat(b.getBackupInfo().storageLocation, RegexMatcher.matchesRegex(storageRegex));
    assertEquals(s3StorageConfig.getConfigUUID(), b.getBackupInfo().storageConfigUUID);
    assertEquals(InProgress, b.getState());
  }

  @Test
  public void testCreateWithNonS3StorageUUID() {
    JsonNode formData =
        Json.parse(
            "{\"name\": \"NFS\", \"configName\": \"Test\", \"type\": \"STORAGE\", \"data\": {}}");
    CustomerConfig customerConfig =
        CustomerConfig.createWithFormData(defaultCustomer.getUuid(), formData);
    UUID universeUUID = UUID.randomUUID();
    BackupTableParams params = new BackupTableParams();
    params.storageConfigUUID = customerConfig.getConfigUUID();
    params.setUniverseUUID(universeUUID);
    params.setKeyspace("foo");
    params.setTableName("bar");
    Backup b = Backup.create(defaultCustomer.getUuid(), params);
    String storageRegex =
        "univ-" + universeUUID + "/backup-[a-zA-Z0-9]+?/full/" + timestampRegex + "/table-foo.bar";
    assertThat(b.getBackupInfo().storageLocation, RegexMatcher.matchesRegex(storageRegex));
    assertEquals(customerConfig.getConfigUUID(), b.getBackupInfo().storageConfigUUID);
    assertEquals(InProgress, b.getState());
  }

  @Test
  public void testCreateWithInvalidConfigName() throws Exception {
    JsonNode formData =
        Json.parse(
            "{\"name\": \"TEST\", \"configName\": \"Test\", \"type\": \"STORAGE\", \"data\": {}}");
    CustomerConfig customerConfig =
        CustomerConfig.createWithFormData(defaultCustomer.getUuid(), formData);
    UUID universeUUID = UUID.randomUUID();
    BackupTableParams params = new BackupTableParams();
    params.storageConfigUUID = customerConfig.getConfigUUID();
    params.setUniverseUUID(universeUUID);
    params.setKeyspace("foo");
    params.setTableName("bar");
    assertThrows(
        "Unknown data type in configuration data. Type STORAGE, name TEST.",
        PlatformServiceException.class,
        () -> Backup.create(defaultCustomer.getUuid(), params));
  }

  @Test
  public void testFetchByUniverseWithValidUUID() {
    Universe u = ModelFactory.createUniverse(defaultCustomer.getId());
    ModelFactory.createBackup(
        defaultCustomer.getUuid(), u.getUniverseUUID(), s3StorageConfig.getConfigUUID());
    List<Backup> backupList =
        Backup.fetchByUniverseUUID(defaultCustomer.getUuid(), u.getUniverseUUID());
    assertEquals(1, backupList.size());
  }

  @Test
  public void testFetchByUniverseWithInvalidUUID() {
    Universe u = ModelFactory.createUniverse(defaultCustomer.getId());
    ModelFactory.createBackup(
        defaultCustomer.getUuid(), u.getUniverseUUID(), s3StorageConfig.getConfigUUID());
    List<Backup> backupList =
        Backup.fetchByUniverseUUID(defaultCustomer.getUuid(), UUID.randomUUID());
    assertEquals(0, backupList.size());
  }

  @Test
  public void testFetchByTaskWithValidUUID() {
    Universe u = ModelFactory.createUniverse(defaultCustomer.getId());
    Backup b =
        ModelFactory.createBackup(
            defaultCustomer.getUuid(), u.getUniverseUUID(), s3StorageConfig.getConfigUUID());
    UUID taskUUID = UUID.randomUUID();
    b.setTaskUUID(taskUUID);
    b.save();
    Backup fb = Backup.fetchAllBackupsByTaskUUID(taskUUID).get(0);
    assertNotNull(fb);
    assertEquals(fb, b);
  }

  @Test
  public void testFetchByTaskWithInvalidBackupUUID() {
    UUID taskUUID = buildTaskInfo(null, TaskType.CreateUniverse);
    CustomerTask ct =
        CustomerTask.create(
            defaultCustomer,
            UUID.randomUUID(),
            taskUUID,
            CustomerTask.TargetType.Backup,
            CustomerTask.TaskType.Create,
            "Demo Backup");
    List<Backup> fb = Backup.fetchAllBackupsByTaskUUID(ct.getTaskUUID());
    assertEquals(0, fb.size());
  }

  @Test
  public void testFetchByTaskWithTargetType() {
    Universe u = ModelFactory.createUniverse(defaultCustomer.getId());
    Backup b =
        ModelFactory.createBackup(
            defaultCustomer.getUuid(), u.getUniverseUUID(), s3StorageConfig.getConfigUUID());
    UUID taskUUID = buildTaskInfo(null, TaskType.CreateUniverse);
    CustomerTask ct =
        CustomerTask.create(
            defaultCustomer,
            b.getBackupUUID(),
            taskUUID,
            CustomerTask.TargetType.Table,
            CustomerTask.TaskType.Create,
            "Demo Backup");
    List<Backup> fb = Backup.fetchAllBackupsByTaskUUID(ct.getTaskUUID());
    assertEquals(0, fb.size());
  }

  @Test
  public void testGetWithValidCustomerUUID() {
    Universe u = ModelFactory.createUniverse(defaultCustomer.getId());
    Backup b =
        ModelFactory.createBackup(
            defaultCustomer.getUuid(), u.getUniverseUUID(), s3StorageConfig.getConfigUUID());
    Backup fb = Backup.get(defaultCustomer.getUuid(), b.getBackupUUID());
    assertEquals(fb, b);
  }

  @Test
  public void testGetWithInvalidCustomerUUID() {
    Universe u = ModelFactory.createUniverse(defaultCustomer.getId());
    Backup b =
        ModelFactory.createBackup(
            defaultCustomer.getUuid(), u.getUniverseUUID(), s3StorageConfig.getConfigUUID());
    Backup fb = Backup.get(UUID.randomUUID(), b.getBackupUUID());
    assertNull(fb);
  }

  @Test
  @Parameters({"InProgress, Completed", "Completed, Deleted", "Completed, FailedToDelete"})
  public void testTransitionStateValid(Backup.BackupState from, Backup.BackupState to) {
    Universe u = ModelFactory.createUniverse(defaultCustomer.getId());
    Backup b =
        ModelFactory.createBackup(
            defaultCustomer.getUuid(), u.getUniverseUUID(), s3StorageConfig.getConfigUUID());
    b.transitionState(from);
    assertEquals(from, b.getState());
    b.transitionState(to);
    assertEquals(to, b.getState());
  }

  @Test
  @Parameters({"Completed, Failed"})
  public void testTransitionStateInvalid(Backup.BackupState from, Backup.BackupState to)
      throws InterruptedException {
    Universe u = ModelFactory.createUniverse(defaultCustomer.getId());
    Backup b =
        ModelFactory.createBackup(
            defaultCustomer.getUuid(), u.getUniverseUUID(), s3StorageConfig.getConfigUUID());
    Date beforeUpdateTime = b.getUpdateTime();
    assertNotNull(b.getUpdateTime());
    Thread.sleep(1);
    b.transitionState(from);
    assertNotNull(b.getUpdateTime());
    assertNotEquals(beforeUpdateTime, b.getUpdateTime());
    b.transitionState(to);
    assertNotEquals(Failed, b.getState());
  }

  @Test
  public void testSetTaskUUIDWhenNull() {
    Universe u = ModelFactory.createUniverse(defaultCustomer.getId());
    Backup b =
        ModelFactory.createBackup(
            defaultCustomer.getUuid(), u.getUniverseUUID(), s3StorageConfig.getConfigUUID());
    UUID taskUUID = UUID.randomUUID();
    assertNull(b.getTaskUUID());
    b.setTaskUUID(taskUUID);
    b.save();
    b.refresh();
    assertEquals(taskUUID, b.getTaskUUID());
  }

  @Test
  public void testSetTaskUUIDWhenNotNull() {
    Universe u = ModelFactory.createUniverse(defaultCustomer.getId());
    Backup b =
        ModelFactory.createBackup(
            defaultCustomer.getUuid(), u.getUniverseUUID(), s3StorageConfig.getConfigUUID());
    b.setTaskUUID(UUID.randomUUID());
    b.save();
    UUID taskUUID = UUID.randomUUID();
    assertNotNull(b.getTaskUUID());
    b.setTaskUUID(taskUUID);
    b.save();
    b.refresh();
    assertEquals(taskUUID, b.getTaskUUID());
  }

  @Test
  public void testGetAllCompletedBackupsWithExpiryForDelete() {
    UUID universeUUID = UUID.randomUUID();
    Universe universe = ModelFactory.createUniverse(defaultCustomer.getId());
    Backup backup1 =
        ModelFactory.createBackupWithExpiry(
            defaultCustomer.getUuid(), universe.getUniverseUUID(), s3StorageConfig.getConfigUUID());
    Backup backup2 =
        ModelFactory.createBackupWithExpiry(
            defaultCustomer.getUuid(), universe.getUniverseUUID(), s3StorageConfig.getConfigUUID());
    Backup backup3 =
        ModelFactory.createBackupWithExpiry(
            defaultCustomer.getUuid(), universeUUID, s3StorageConfig.getConfigUUID());
    Backup backup4 =
        ModelFactory.createBackupWithExpiry(
            defaultCustomer.getUuid(), universeUUID, s3StorageConfig.getConfigUUID());
    Backup backup5 =
        ModelFactory.createBackupWithExpiry(
            defaultCustomer.getUuid(), universeUUID, s3StorageConfig.getConfigUUID());

    backup1.transitionState(Backup.BackupState.Completed);
    backup2.transitionState(Backup.BackupState.Completed);
    backup3.transitionState(Backup.BackupState.Completed);
    backup4.transitionState(Backup.BackupState.QueuedForDeletion);
    backup5.transitionState(Backup.BackupState.FailedToDelete);

    Map<UUID, List<Backup>> expiredBackups = Backup.getCompletedExpiredBackups();
    // assert to 3 as we are deleting backups even if the universe does not exist.
    assertEquals(
        String.valueOf(expiredBackups), 3, expiredBackups.get(defaultCustomer.getUuid()).size());
  }

  @Test
  public void testDeserializationJson() {
    String jsonWithUnknownFields =
        "{\"errorString\":null,\"deviceInfo\":null,"
            + "\"universeUUID\":\"2ca2d8aa-2879-41b5-8267-4dccc789e841\",\"expectedUniverseVersion\":0,"
            + "\"enableEncryptionAtRest\":false,\"disableEncryptionAtRest\":false,\"cmkArn\":n"
            + "ull,\"encryptionAtRestConfig\":null,\"nodeDetailsSet\":null,"
            + "\"keyspace\":\"system_redis\",\"tableName\":\"redis\","
            + "\"tableUUID\":\"33268a1e-33e0-4606-90c6-ff9fb7e8c896\",\"sse\":false,"
            + "\"storageConfigUUID\":\"c0432198-df18-40cb-a012-83c89ca63573\","
            + "\"storageLocation\":\"s3://backups.yugabyte.com/por"
            + "tal/univ-2ca2d8aa-2879-41b5-8267-4dccc789e841/backup-2019-11-19T04:21:48-391451651/table"
            + "-system_redis.redis-33268a1e33e0460690c6ff9fb7e8c896\",\"actionType\":\"CREATE\","
            + "\"schedulingFrequency\":30000,\"timeBeforeDelete\":0,\"enableVerboseLogs\":false}";
    UUID universeUUID = UUID.randomUUID();
    Backup b =
        ModelFactory.createBackup(
            defaultCustomer.getUuid(), universeUUID, s3StorageConfig.getConfigUUID());
    assertNotNull(b);

    SqlUpdate sqlUpdate =
        DB.sqlUpdate(
            "update public.backup set backup_info = '"
                + jsonWithUnknownFields
                + "'  where backup_uuid::text = '"
                + b.getBackupUUID()
                + "';");
    sqlUpdate.execute();

    b = Backup.get(defaultCustomer.getUuid(), b.getBackupUUID());
    assertNotNull(b);
  }

  private BackupTableParams createTableParams(int lower, int upper, String keyspace) {
    List<UUID> tableUUIDs =
        Arrays.asList(
            UUID.fromString("00000000-0000-0000-0000-000000000000"),
            UUID.fromString("00000000-0000-0000-0000-000000000001"),
            UUID.fromString("00000000-0000-0000-0000-000000000002"),
            UUID.fromString("00000000-0000-0000-0000-000000000003"));
    List<String> tableNames = Arrays.asList("t1", "t2", "t3", "t4");
    BackupTableParams params = new BackupTableParams();
    params.setKeyspace(keyspace);
    params.storageConfigUUID = s3StorageConfig.getConfigUUID();
    params.tableNameList = new ArrayList<>(tableNames.subList(lower, upper));
    params.tableUUIDList = new ArrayList<>(tableUUIDs.subList(lower, upper));
    return params;
  }

  private BackupTableParams createParentParams(Universe universe) {
    BackupTableParams tableParamsParent = new BackupTableParams();
    tableParamsParent.setUniverseUUID(universe.getUniverseUUID());
    tableParamsParent.customerUuid = defaultCustomer.getUuid();
    tableParamsParent.storageConfigUUID = s3StorageConfig.getConfigUUID();
    tableParamsParent.backupType = TableType.YQL_TABLE_TYPE;
    return tableParamsParent;
  }

  @Test
  public void testCreateBackupWithPreviousBackupAllOverlap() {
    Universe defaultUniverse = ModelFactory.createUniverse(defaultCustomer.getId());
    BackupTableParams tableParamsParent = createParentParams(defaultUniverse);
    List<BackupTableParams> paramsList = new ArrayList<>();
    BackupTableParams childParam1 = createTableParams(0, 2, "foo");
    paramsList.add(childParam1);
    BackupTableParams childParam2 = createTableParams(2, 4, "bar");
    paramsList.add(childParam2);
    tableParamsParent.backupList = new ArrayList<>(paramsList);
    Backup previousBackup =
        Backup.create(
            defaultCustomer.getUuid(),
            tableParamsParent,
            BackupCategory.YB_CONTROLLER,
            BackupVersion.V2);
    previousBackup.transitionState(BackupState.Completed);
    UUID baseIdentifier_1 = previousBackup.getBackupInfo().backupList.get(0).backupParamsIdentifier;
    UUID baseIdentifier_2 = previousBackup.getBackupInfo().backupList.get(1).backupParamsIdentifier;

    BackupTableParams tableParamsParentIncrement = createParentParams(defaultUniverse);
    tableParamsParentIncrement.baseBackupUUID = tableParamsParent.backupUuid;

    List<BackupTableParams> paramsListIncrement = new ArrayList<>();
    BackupTableParams childParam1Increment = createTableParams(0, 2, "foo");
    childParam1Increment.setUniverseUUID(defaultUniverse.getUniverseUUID());
    paramsListIncrement.add(childParam1Increment);

    BackupTableParams childParam2Increment = createTableParams(2, 4, "bar");
    childParam2Increment.setUniverseUUID(defaultUniverse.getUniverseUUID());
    paramsListIncrement.add(childParam2Increment);
    tableParamsParentIncrement.backupList = new ArrayList<>(paramsListIncrement);
    // Incremental backup type 1 - same tableParams
    Backup increment1 =
        Backup.create(
            defaultCustomer.getUuid(),
            tableParamsParentIncrement,
            BackupCategory.YB_CONTROLLER,
            BackupVersion.V2);
    UUID increment1_identifier_1 =
        increment1.getBackupInfo().backupList.get(0).backupParamsIdentifier;
    UUID increment1_identifier_2 =
        increment1.getBackupInfo().backupList.get(1).backupParamsIdentifier;
    assertEquals(increment1_identifier_1, baseIdentifier_1);
    assertEquals(increment1_identifier_2, baseIdentifier_2);
    String storageRegex =
        "s3://foo/univ-"
            + defaultUniverse.getUniverseUUID()
            + "/ybc_backup-[a-zA-Z0-9]+?/incremental/"
            + timestampRegex
            + "/.+";
    assertThat(
        increment1.getBackupInfo().backupList.get(0).storageLocation,
        RegexMatcher.matchesRegex(storageRegex));
  }

  @Test
  public void testCreateBackupWithPreviousBackupPartialOverlap() {
    Universe defaultUniverse = ModelFactory.createUniverse(defaultCustomer.getId());
    BackupTableParams tableParamsParent = createParentParams(defaultUniverse);
    List<BackupTableParams> paramsList = new ArrayList<>();
    BackupTableParams childParam1 = createTableParams(0, 2, "foo");
    paramsList.add(childParam1);
    BackupTableParams childParam2 = createTableParams(2, 3, "bar");
    paramsList.add(childParam2);
    tableParamsParent.backupList = new ArrayList<>(paramsList);
    Backup previousBackup =
        Backup.create(
            defaultCustomer.getUuid(),
            tableParamsParent,
            BackupCategory.YB_CONTROLLER,
            BackupVersion.V2);
    previousBackup.transitionState(BackupState.Completed);
    UUID baseIdentifier_1 = previousBackup.getBackupInfo().backupList.get(0).backupParamsIdentifier;
    UUID baseIdentifier_2 = previousBackup.getBackupInfo().backupList.get(1).backupParamsIdentifier;

    BackupTableParams tableParamsParent2 = createParentParams(defaultUniverse);
    List<BackupTableParams> paramsList2 = new ArrayList<>();
    paramsList2.add(childParam1);
    paramsList2.add(childParam2);
    BackupTableParams childParam3 = createTableParams(3, 4, "bar");
    paramsList2.add(childParam3);
    tableParamsParent2.backupList = paramsList2;
    tableParamsParent2.baseBackupUUID = previousBackup.getBaseBackupUUID();
    Backup increment1 =
        Backup.create(
            defaultCustomer.getUuid(),
            tableParamsParent2,
            BackupCategory.YB_CONTROLLER,
            BackupVersion.V2);
    UUID increment1_identifier_1 =
        increment1.getBackupInfo().backupList.get(0).backupParamsIdentifier;
    UUID increment1_identifier_2 =
        increment1.getBackupInfo().backupList.get(1).backupParamsIdentifier;
    UUID increment1_identifier_3 =
        increment1.getBackupInfo().backupList.get(2).backupParamsIdentifier;

    assertEquals(increment1_identifier_1, baseIdentifier_1);
    assertEquals(increment1_identifier_2, baseIdentifier_2);
    assertNotEquals(increment1_identifier_3, baseIdentifier_2);
  }
}
