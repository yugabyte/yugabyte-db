// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;

import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Backup.BackupState;
import com.yugabyte.yw.models.configs.CustomerConfig;
import java.util.UUID;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import junitparams.naming.TestCaseName;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(JUnitParamsRunner.class)
public class DeleteBackupYbTest extends FakeDBApplication {
  private Customer defaultCustomer;
  private Backup backup;
  private CustomerConfig s3StorageConfig;

  @Before
  public void setUp() {
    UUID universeUUID = UUID.randomUUID();
    defaultCustomer = ModelFactory.testCustomer();
    s3StorageConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST1");
    backup =
        ModelFactory.createBackup(defaultCustomer.uuid, universeUUID, s3StorageConfig.configUUID);
  }

  @Test
  @Parameters({"InProgress", "DeleteInProgress", "QueuedForDeletion"})
  @TestCaseName("testFailureWithInValidStateWhenState:{0}")
  public void testFailureWithInValidState(BackupState state) {
    backup.transitionState(state);
    DeleteBackupYb.Params params = new DeleteBackupYb.Params();
    params.backupUUID = backup.backupUUID;
    params.customerUUID = defaultCustomer.uuid;
    DeleteBackupYb deleteBackupTask = AbstractTaskBase.createTask(DeleteBackupYb.class);
    deleteBackupTask.initialize(params);
    deleteBackupTask.run();
    backup.refresh();
    assertEquals(state, backup.state);
  }

  @Test
  @TestCaseName("testSuccessWithValidStateWhenState:{0}")
  @Parameters({"Failed", "Skipped", "FailedToDelete", "Stopped", "Completed"})
  public void testSuccessWithValidState(BackupState state) {
    backup.transitionState(state);
    DeleteBackupYb.Params params = new DeleteBackupYb.Params();
    params.backupUUID = backup.backupUUID;
    params.customerUUID = defaultCustomer.uuid;
    DeleteBackupYb deleteBackupTask = AbstractTaskBase.createTask(DeleteBackupYb.class);
    deleteBackupTask.initialize(params);
    deleteBackupTask.run();
    backup.refresh();
    assertEquals(BackupState.QueuedForDeletion, backup.state);
  }

  @Test
  public void testFailureWithInvalidStorageConfigUUID() {
    UUID invalidStorageConfigUUID = UUID.randomUUID();
    Backup backup =
        ModelFactory.createBackup(
            defaultCustomer.uuid, UUID.randomUUID(), invalidStorageConfigUUID);
    backup.transitionState(BackupState.Completed);
    DeleteBackupYb.Params params = new DeleteBackupYb.Params();
    params.backupUUID = backup.backupUUID;
    params.customerUUID = defaultCustomer.uuid;
    DeleteBackupYb deleteBackupTask = AbstractTaskBase.createTask(DeleteBackupYb.class);
    deleteBackupTask.initialize(params);
    RuntimeException re = assertThrows(RuntimeException.class, () -> deleteBackupTask.run());
    assertEquals("Invalid StorageConfig UUID: " + invalidStorageConfigUUID, re.getMessage());
    backup.refresh();
    assertEquals(BackupState.FailedToDelete, backup.state);
  }

  @Test
  public void testFailureWithInProgressIncrementalBackup() {
    Backup fullBackup =
        ModelFactory.createBackup(
            defaultCustomer.uuid, UUID.randomUUID(), s3StorageConfig.configUUID);
    fullBackup.transitionState(BackupState.Completed);
    DeleteBackupYb.Params params = new DeleteBackupYb.Params();
    params.backupUUID = fullBackup.backupUUID;
    params.customerUUID = defaultCustomer.uuid;
    Backup incrementalBackup1 =
        ModelFactory.createBackup(
            defaultCustomer.uuid, UUID.randomUUID(), s3StorageConfig.configUUID);
    Backup incrementalBackup2 =
        ModelFactory.createBackup(
            defaultCustomer.uuid, UUID.randomUUID(), s3StorageConfig.configUUID);
    incrementalBackup1.transitionState(BackupState.Completed);
    incrementalBackup1.baseBackupUUID = fullBackup.backupUUID;
    incrementalBackup1.save();
    incrementalBackup2.transitionState(BackupState.InProgress);
    incrementalBackup2.baseBackupUUID = fullBackup.backupUUID;
    incrementalBackup2.save();
    DeleteBackupYb deleteBackupTask = AbstractTaskBase.createTask(DeleteBackupYb.class);
    deleteBackupTask.initialize(params);
    RuntimeException re = assertThrows(RuntimeException.class, () -> deleteBackupTask.run());
    assertEquals(
        "Cannot delete backup "
            + fullBackup.backupUUID
            + " as a incremental/full backup is in progress.",
        re.getMessage());
    fullBackup.refresh();
    assertEquals(BackupState.Completed, fullBackup.state);
    incrementalBackup1.refresh();
    assertEquals(BackupState.Completed, incrementalBackup1.state);
    incrementalBackup2.refresh();
    assertEquals(BackupState.InProgress, incrementalBackup2.state);
  }

  @Test
  public void testDeleteIncrementalBackupChain() {
    Backup fullBackup =
        ModelFactory.createBackup(
            defaultCustomer.uuid, UUID.randomUUID(), s3StorageConfig.configUUID);
    fullBackup.transitionState(BackupState.Completed);
    DeleteBackupYb.Params params = new DeleteBackupYb.Params();
    params.backupUUID = fullBackup.backupUUID;
    params.customerUUID = defaultCustomer.uuid;
    Backup incrementalBackup1 =
        ModelFactory.createBackup(
            defaultCustomer.uuid, UUID.randomUUID(), s3StorageConfig.configUUID);
    Backup incrementalBackup2 =
        ModelFactory.createBackup(
            defaultCustomer.uuid, UUID.randomUUID(), s3StorageConfig.configUUID);
    incrementalBackup1.transitionState(BackupState.Completed);
    incrementalBackup1.baseBackupUUID = fullBackup.backupUUID;
    incrementalBackup1.save();
    incrementalBackup2.transitionState(BackupState.Completed);
    incrementalBackup2.baseBackupUUID = fullBackup.backupUUID;
    incrementalBackup2.save();
    DeleteBackupYb deleteBackupTask = AbstractTaskBase.createTask(DeleteBackupYb.class);
    deleteBackupTask.initialize(params);
    deleteBackupTask.run();
    fullBackup.refresh();
    assertEquals(BackupState.QueuedForDeletion, fullBackup.state);
    incrementalBackup1.refresh();
    assertEquals(BackupState.QueuedForDeletion, incrementalBackup1.state);
    incrementalBackup2.refresh();
    assertEquals(BackupState.QueuedForDeletion, incrementalBackup2.state);
  }
}
