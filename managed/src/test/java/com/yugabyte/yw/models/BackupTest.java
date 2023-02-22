// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import static com.yugabyte.yw.models.Backup.BackupState.Failed;
import static com.yugabyte.yw.models.Backup.BackupState.InProgress;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThat;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.RegexMatcher;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.models.configs.CustomerConfig;
import io.ebean.Ebean;
import io.ebean.SqlUpdate;
import java.util.Date;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import play.libs.Json;

@RunWith(JUnitParamsRunner.class)
public class BackupTest extends FakeDBApplication {
  private Customer defaultCustomer;
  private CustomerConfig s3StorageConfig;

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
            defaultCustomer.getUuid(), universeUUID, s3StorageConfig.configUUID);
    assertNotNull(b);
    String storageRegex =
        "s3://foo/univ-"
            + universeUUID
            + "/backup-\\d{4}-[0-1]\\d-[0-3]\\dT[0-2]\\d:[0-5]\\d:[0-5]\\d\\-\\d+/table-foo.bar-[a-zA-Z0-9]*";
    assertThat(b.getBackupInfo().storageLocation, RegexMatcher.matchesRegex(storageRegex));
    assertEquals(s3StorageConfig.configUUID, b.getBackupInfo().storageConfigUUID);
    assertEquals(InProgress, b.getState());
  }

  @Test
  public void testCreateWithoutTableUUID() {
    UUID universeUUID = UUID.randomUUID();
    BackupTableParams params = new BackupTableParams();
    params.storageConfigUUID = s3StorageConfig.configUUID;
    params.universeUUID = universeUUID;
    params.setKeyspace("foo");
    params.setTableName("bar");
    Backup b = Backup.create(defaultCustomer.getUuid(), params);
    String storageRegex =
        "s3://foo/univ-"
            + universeUUID
            + "/backup-\\d{4}-[0-1]\\d-[0-3]\\dT[0-2]\\d:[0-5]\\d:[0-5]\\d\\-\\d+/table-foo.bar";
    assertThat(b.getBackupInfo().storageLocation, RegexMatcher.matchesRegex(storageRegex));
    assertEquals(s3StorageConfig.configUUID, b.getBackupInfo().storageConfigUUID);
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
    params.storageConfigUUID = customerConfig.configUUID;
    params.universeUUID = universeUUID;
    params.setKeyspace("foo");
    params.setTableName("bar");
    Backup b = Backup.create(defaultCustomer.getUuid(), params);
    String storageRegex =
        "univ-"
            + universeUUID
            + "/backup-\\d{4}-[0-1]\\d-[0-3]\\dT[0-2]\\d:[0-5]\\d:[0-5]\\d\\-\\d+/table-foo.bar";
    assertThat(b.getBackupInfo().storageLocation, RegexMatcher.matchesRegex(storageRegex));
    assertEquals(customerConfig.configUUID, b.getBackupInfo().storageConfigUUID);
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
    params.storageConfigUUID = customerConfig.configUUID;
    params.universeUUID = universeUUID;
    params.setKeyspace("foo");
    params.setTableName("bar");
    assertThrows(
        "Unknown data type in configuration data. Type STORAGE, name TEST.",
        PlatformServiceException.class,
        () -> Backup.create(defaultCustomer.getUuid(), params));
  }

  @Test
  public void testFetchByUniverseWithValidUUID() {
    Universe u = ModelFactory.createUniverse(defaultCustomer.getCustomerId());
    ModelFactory.createBackup(
        defaultCustomer.getUuid(), u.getUniverseUUID(), s3StorageConfig.configUUID);
    List<Backup> backupList =
        Backup.fetchByUniverseUUID(defaultCustomer.getUuid(), u.getUniverseUUID());
    assertEquals(1, backupList.size());
  }

  @Test
  public void testFetchByUniverseWithInvalidUUID() {
    Universe u = ModelFactory.createUniverse(defaultCustomer.getCustomerId());
    ModelFactory.createBackup(
        defaultCustomer.getUuid(), u.getUniverseUUID(), s3StorageConfig.configUUID);
    List<Backup> backupList =
        Backup.fetchByUniverseUUID(defaultCustomer.getUuid(), UUID.randomUUID());
    assertEquals(0, backupList.size());
  }

  @Test
  public void testFetchByTaskWithValidUUID() {
    Universe u = ModelFactory.createUniverse(defaultCustomer.getCustomerId());
    Backup b =
        ModelFactory.createBackup(
            defaultCustomer.getUuid(), u.getUniverseUUID(), s3StorageConfig.configUUID);
    UUID taskUUID = UUID.randomUUID();
    b.assignTaskUuid(taskUUID);
    Backup fb = Backup.fetchAllBackupsByTaskUUID(taskUUID).get(0);
    assertNotNull(fb);
    assertEquals(fb, b);
  }

  @Test
  public void testFetchByTaskWithInvalidBackupUUID() {
    CustomerTask ct =
        CustomerTask.create(
            defaultCustomer,
            UUID.randomUUID(),
            UUID.randomUUID(),
            CustomerTask.TargetType.Backup,
            CustomerTask.TaskType.Create,
            "Demo Backup");
    List<Backup> fb = Backup.fetchAllBackupsByTaskUUID(ct.getTaskUUID());
    assertEquals(0, fb.size());
  }

  @Test
  public void testFetchByTaskWithTargetType() {
    Universe u = ModelFactory.createUniverse(defaultCustomer.getCustomerId());
    Backup b =
        ModelFactory.createBackup(
            defaultCustomer.getUuid(), u.getUniverseUUID(), s3StorageConfig.configUUID);
    CustomerTask ct =
        CustomerTask.create(
            defaultCustomer,
            b.getBackupUUID(),
            UUID.randomUUID(),
            CustomerTask.TargetType.Table,
            CustomerTask.TaskType.Create,
            "Demo Backup");
    List<Backup> fb = Backup.fetchAllBackupsByTaskUUID(ct.getTaskUUID());
    assertEquals(0, fb.size());
  }

  @Test
  public void testGetWithValidCustomerUUID() {
    Universe u = ModelFactory.createUniverse(defaultCustomer.getCustomerId());
    Backup b =
        ModelFactory.createBackup(
            defaultCustomer.getUuid(), u.getUniverseUUID(), s3StorageConfig.configUUID);
    Backup fb = Backup.get(defaultCustomer.getUuid(), b.getBackupUUID());
    assertEquals(fb, b);
  }

  @Test
  public void testGetWithInvalidCustomerUUID() {
    Universe u = ModelFactory.createUniverse(defaultCustomer.getCustomerId());
    Backup b =
        ModelFactory.createBackup(
            defaultCustomer.getUuid(), u.getUniverseUUID(), s3StorageConfig.configUUID);
    Backup fb = Backup.get(UUID.randomUUID(), b.getBackupUUID());
    assertNull(fb);
  }

  @Test
  @Parameters({"InProgress, Completed", "Completed, Deleted", "Completed, FailedToDelete"})
  public void testTransitionStateValid(Backup.BackupState from, Backup.BackupState to) {
    Universe u = ModelFactory.createUniverse(defaultCustomer.getCustomerId());
    Backup b =
        ModelFactory.createBackup(
            defaultCustomer.getUuid(), u.getUniverseUUID(), s3StorageConfig.configUUID);
    b.transitionState(from);
    assertEquals(from, b.getState());
    b.transitionState(to);
    assertEquals(to, b.getState());
  }

  @Test
  @Parameters({"Completed, Failed"})
  public void testTransitionStateInvalid(Backup.BackupState from, Backup.BackupState to)
      throws InterruptedException {
    Universe u = ModelFactory.createUniverse(defaultCustomer.getCustomerId());
    Backup b =
        ModelFactory.createBackup(
            defaultCustomer.getUuid(), u.getUniverseUUID(), s3StorageConfig.configUUID);
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
    Universe u = ModelFactory.createUniverse(defaultCustomer.getCustomerId());
    Backup b =
        ModelFactory.createBackup(
            defaultCustomer.getUuid(), u.getUniverseUUID(), s3StorageConfig.configUUID);
    UUID taskUUID = UUID.randomUUID();
    assertNull(b.getTaskUUID());
    b.assignTaskUuid(taskUUID);
    b.refresh();
    assertEquals(taskUUID, b.getTaskUUID());
  }

  @Test
  public void testSetTaskUUID() throws InterruptedException {
    Universe u = ModelFactory.createUniverse(defaultCustomer.getCustomerId());
    Backup b =
        ModelFactory.createBackup(
            defaultCustomer.getUuid(), u.getUniverseUUID(), s3StorageConfig.configUUID);
    UUID taskUUID1 = UUID.randomUUID();
    UUID taskUUID2 = UUID.randomUUID();
    ExecutorService service = Executors.newFixedThreadPool(2);
    AtomicBoolean success1 = new AtomicBoolean();
    AtomicBoolean success2 = new AtomicBoolean();
    service.submit(() -> success1.set(b.assignTaskUuid(taskUUID1)));
    service.submit(() -> success2.set(b.assignTaskUuid(taskUUID2)));
    service.awaitTermination(3, TimeUnit.SECONDS);
    b.refresh();
    if (success1.get() && !success2.get()) {
      assertEquals(taskUUID2, b.getTaskUUID());
    } else {
      assertFalse(success1.get());
      assertTrue(success2.get());
      assertEquals(taskUUID2, b.getTaskUUID());
    }
  }

  @Test
  public void testSetTaskUUIDWhenNotNull() {
    Universe u = ModelFactory.createUniverse(defaultCustomer.getCustomerId());
    Backup b =
        ModelFactory.createBackup(
            defaultCustomer.getUuid(), u.getUniverseUUID(), s3StorageConfig.configUUID);
    b.assignTaskUuid(UUID.randomUUID());
    UUID taskUUID = UUID.randomUUID();
    assertNotNull(b.getTaskUUID());
    b.assignTaskUuid(taskUUID);
    b.refresh();
    assertEquals(taskUUID, b.getTaskUUID());
  }

  public void testGetAllCompletedBackupsWithExpiryForDelete() {
    UUID universeUUID = UUID.randomUUID();
    Universe universe = ModelFactory.createUniverse(defaultCustomer.getCustomerId());
    Backup backup1 =
        ModelFactory.createBackupWithExpiry(
            defaultCustomer.getUuid(), universe.getUniverseUUID(), s3StorageConfig.configUUID);
    Backup backup2 =
        ModelFactory.createBackupWithExpiry(
            defaultCustomer.getUuid(), universe.getUniverseUUID(), s3StorageConfig.configUUID);
    Backup backup3 =
        ModelFactory.createBackupWithExpiry(
            defaultCustomer.getUuid(), universeUUID, s3StorageConfig.configUUID);

    backup1.transitionState(Backup.BackupState.Completed);
    backup2.transitionState(Backup.BackupState.Completed);
    backup3.transitionState(Backup.BackupState.Completed);

    Map<Customer, List<Backup>> expiredBackups = Backup.getExpiredBackups();
    // assert to 3 as we are deleting backups even if the universe does not exist.
    assertEquals(String.valueOf(expiredBackups), 3, expiredBackups.get(defaultCustomer).size());
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
            defaultCustomer.getUuid(), universeUUID, s3StorageConfig.configUUID);
    assertNotNull(b);

    SqlUpdate sqlUpdate =
        Ebean.createSqlUpdate(
            "update public.backup set backup_info = '"
                + jsonWithUnknownFields
                + "'  where backup_uuid::text = '"
                + b.getBackupUUID()
                + "';");
    sqlUpdate.execute();

    b = Backup.get(defaultCustomer.getUuid(), b.getBackupUUID());
    assertNotNull(b);
  }
}
