// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.TaskExecutionException;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Backup.BackupState;
import com.yugabyte.yw.models.ContinuousBackupConfig;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.configs.CustomerConfig.ConfigState;
import com.yugabyte.yw.models.helpers.TimeUnit;
import org.junit.Before;
import org.junit.Test;

public class DeleteStorageConfigTest extends FakeDBApplication {

  private Customer customer;
  private Universe universe;
  private CustomerConfig config;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    universe = ModelFactory.createUniverse("backed-up", customer.getId());
    config = ModelFactory.createNfsStorageConfig(customer, "nfs", "/mnt/nfs");
  }

  private void run(boolean deleteBackups) {
    DeleteStorageConfig.Params params = new DeleteStorageConfig.Params();
    params.customerUUID = customer.getUuid();
    params.configUUID = config.getConfigUUID();
    params.deleteBackups = deleteBackups;
    DeleteStorageConfig task =
        new DeleteStorageConfig(app.injector().instanceOf(BaseTaskDependencies.class));
    task.initialize(params);
    task.run();
  }

  private Backup backup(BackupState state) {
    Backup backup =
        ModelFactory.createBackup(
            customer.getUuid(), universe.getUniverseUUID(), config.getConfigUUID());
    if (state != BackupState.InProgress) {
      backup.setState(state);
      backup.save();
    }
    return backup;
  }

  private CustomerConfig reloaded() {
    return CustomerConfig.get(customer.getUuid(), config.getConfigUUID());
  }

  private static BackupState stateOf(Backup backup) {
    return Backup.getOrBadRequest(backup.getCustomerUUID(), backup.getBackupUUID()).getState();
  }

  private void assertInUse(boolean deleteBackups, String messagePart) {
    TaskExecutionException e = assertThrows(TaskExecutionException.class, () -> run(deleteBackups));
    assertTrue(e.getMessage(), e.getMessage().startsWith("STORAGE_CONFIG_IN_USE: "));
    assertTrue(e.getMessage(), e.getMessage().contains(messagePart));
    assertEquals(ConfigState.Active, reloaded().getState());
  }

  @Test
  public void removesAnUnusedConfig() {
    // Failed backups don't keep the config, as with the v1 delete.
    Backup failed = backup(BackupState.Failed);

    run(false);

    assertNull(reloaded());
    assertEquals(BackupState.Failed, stateOf(failed));
  }

  @Test
  public void queuesTheConfigWhileItsBackupsAreBeingDeleted() {
    // The garbage collector needs the config to delete these backups' files.
    Backup deleting = backup(BackupState.QueuedForDeletion);

    run(false);

    assertEquals(ConfigState.QueuedForDeletion, reloaded().getState());
    assertEquals(BackupState.QueuedForDeletion, stateOf(deleting));
  }

  @Test
  public void deleteBackupsQueuesThemAndTheConfig() {
    Backup completed = backup(BackupState.Completed);
    Backup failed = backup(BackupState.Failed);
    Backup deleted = backup(BackupState.Deleted);

    run(true);

    assertEquals(ConfigState.QueuedForDeletion, reloaded().getState());
    assertEquals(BackupState.QueuedForDeletion, stateOf(completed));
    assertEquals(BackupState.QueuedForDeletion, stateOf(failed));
    assertEquals(BackupState.Deleted, stateOf(deleted));
  }

  @Test
  public void completedBackupsKeepTheConfigWithoutDeleteBackups() {
    Backup completed = backup(BackupState.Completed);

    assertInUse(false, "1 completed backup(s)");
    assertEquals(BackupState.Completed, stateOf(completed));
  }

  @Test
  public void aRunningBackupKeepsTheConfigEvenWithDeleteBackups() {
    Backup completed = backup(BackupState.Completed);
    backup(BackupState.InProgress);

    assertInUse(true, "1 running or stopping backup(s)");
    assertEquals(BackupState.Completed, stateOf(completed));
  }

  @Test
  public void backupsWhoseDeleteFailedKeepTheConfigWithoutDeleteBackups() {
    // Their files are still in the storage: without the config, nothing could delete them.
    Backup failedToDelete = backup(BackupState.FailedToDelete);

    assertInUse(false, "1 backup(s) whose delete failed");

    run(true);
    assertEquals(ConfigState.QueuedForDeletion, reloaded().getState());
    assertEquals(BackupState.QueuedForDeletion, stateOf(failedToDelete));
  }

  @Test
  public void ybaContinuousBackupKeepsTheConfig() {
    ContinuousBackupConfig.create(config.getConfigUUID(), 5, TimeUnit.MINUTES, 3, "dir");

    assertInUse(true, "YBA continuous backup");
  }

  @Test
  public void aBackupBeingStoppedKeepsTheConfig() {
    // Its task still writes to the storage until the backup is Stopped.
    Backup stopping = backup(BackupState.Stopping);

    assertInUse(true, "1 running or stopping backup(s)");
    assertEquals(BackupState.Stopping, stateOf(stopping));
  }

  @Test
  public void aScheduleKeepsTheConfig() {
    ModelFactory.createScheduleBackup(
        customer.getUuid(), universe.getUniverseUUID(), config.getConfigUUID());

    assertInUse(true, "backup schedule(s)");
  }

  @Test
  public void anotherCustomersBackupsDontCount() {
    Customer other = ModelFactory.testCustomer("other-customer");
    ModelFactory.createBackup(other.getUuid(), universe.getUniverseUUID(), config.getConfigUUID());

    run(false);

    assertNull(reloaded());
  }

  @Test
  public void aConfigAlreadyQueuedOrDeletedIsLeftAsItIs() {
    config.setState(ConfigState.QueuedForDeletion);
    config.update();
    run(false);
    assertNotNull(reloaded());
    assertEquals(ConfigState.QueuedForDeletion, reloaded().getState());

    config.delete();
    run(true);
    assertNull(reloaded());
  }

  @Test
  public void aValidatingConfigIsntDeleted() {
    config.setState(ConfigState.Validating);
    config.update();

    TaskExecutionException e = assertThrows(TaskExecutionException.class, () -> run(false));
    assertTrue(e.getMessage(), e.getMessage().startsWith("STORAGE_CONFIG_NOT_ACTIVE: "));
    assertEquals(ConfigState.Validating, reloaded().getState());
  }
}
