// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.doThrow;

import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Backup.BackupState;
import com.yugabyte.yw.models.Customer;
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
        ModelFactory.createBackup(
            defaultCustomer.getUuid(), universeUUID, s3StorageConfig.getConfigUUID());
  }

  @Test
  @Parameters({"InProgress", "DeleteInProgress", "QueuedForDeletion"})
  @TestCaseName("testFailureWithInValidStateWhenState:{0}")
  public void testFailureWithInValidState(BackupState state) {
    backup.setState(state);
    backup.save();
    DeleteBackupYb.Params params = new DeleteBackupYb.Params();
    params.backupUUID = backup.getBackupUUID();
    params.customerUUID = defaultCustomer.getUuid();
    DeleteBackupYb deleteBackupTask = AbstractTaskBase.createTask(DeleteBackupYb.class);
    deleteBackupTask.initialize(params);
    deleteBackupTask.run();
    backup.refresh();
    assertEquals(state, backup.getState());
  }

  @Test
  @TestCaseName("testSuccessWithValidStateWhenState:{0}")
  @Parameters({"Failed", "Skipped", "FailedToDelete", "Stopped", "Completed"})
  public void testSuccessWithValidState(BackupState state) {
    backup.transitionState(state);
    DeleteBackupYb.Params params = new DeleteBackupYb.Params();
    params.backupUUID = backup.getBackupUUID();
    params.customerUUID = defaultCustomer.getUuid();
    DeleteBackupYb deleteBackupTask = AbstractTaskBase.createTask(DeleteBackupYb.class);
    deleteBackupTask.initialize(params);
    deleteBackupTask.run();
    backup.refresh();
    assertEquals(BackupState.QueuedForDeletion, backup.getState());
  }

  @Test
  public void testFailureWithInvalidStorageConfigUUID() {
    UUID invalidStorageConfigUUID = UUID.randomUUID();
    Backup backup =
        ModelFactory.createBackup(
            defaultCustomer.getUuid(), UUID.randomUUID(), invalidStorageConfigUUID);
    backup.transitionState(BackupState.Completed);
    DeleteBackupYb.Params params = new DeleteBackupYb.Params();
    params.backupUUID = backup.getBackupUUID();
    params.customerUUID = defaultCustomer.getUuid();
    DeleteBackupYb deleteBackupTask = AbstractTaskBase.createTask(DeleteBackupYb.class);
    deleteBackupTask.initialize(params);
    doThrow(new RuntimeException("Invalid StorageConfig UUID: " + invalidStorageConfigUUID))
        .when(mockBackupHelper)
        .validateStorageConfigOnBackup(any());
    RuntimeException re = assertThrows(RuntimeException.class, () -> deleteBackupTask.run());
    assertEquals("Invalid StorageConfig UUID: " + invalidStorageConfigUUID, re.getMessage());
    backup.refresh();
    assertEquals(BackupState.FailedToDelete, backup.getState());
  }

  @Test
  public void testFailureWithInProgressIncrementalBackup() {
    Backup fullBackup =
        ModelFactory.createBackup(
            defaultCustomer.getUuid(), UUID.randomUUID(), s3StorageConfig.getConfigUUID());
    fullBackup.transitionState(BackupState.Completed);
    DeleteBackupYb.Params params = new DeleteBackupYb.Params();
    params.backupUUID = fullBackup.getBackupUUID();
    params.customerUUID = defaultCustomer.getUuid();
    Backup incrementalBackup1 =
        ModelFactory.createBackup(
            defaultCustomer.getUuid(), UUID.randomUUID(), s3StorageConfig.getConfigUUID());
    Backup incrementalBackup2 =
        ModelFactory.createBackup(
            defaultCustomer.getUuid(), UUID.randomUUID(), s3StorageConfig.getConfigUUID());
    incrementalBackup1.transitionState(BackupState.Completed);
    incrementalBackup1.setBaseBackupUUID(fullBackup.getBackupUUID());
    incrementalBackup1.save();
    incrementalBackup2.transitionState(BackupState.InProgress);
    incrementalBackup2.setBaseBackupUUID(fullBackup.getBackupUUID());
    incrementalBackup2.save();
    DeleteBackupYb deleteBackupTask = AbstractTaskBase.createTask(DeleteBackupYb.class);
    deleteBackupTask.initialize(params);
    RuntimeException re = assertThrows(RuntimeException.class, () -> deleteBackupTask.run());
    assertEquals(
        "Cannot delete backup "
            + fullBackup.getBackupUUID()
            + " as a incremental/full backup is in progress.",
        re.getMessage());
    fullBackup.refresh();
    assertEquals(BackupState.Completed, fullBackup.getState());
    incrementalBackup1.refresh();
    assertEquals(BackupState.Completed, incrementalBackup1.getState());
    incrementalBackup2.refresh();
    assertEquals(BackupState.InProgress, incrementalBackup2.getState());
  }

  @Test
  public void testDeleteIncrementalBackupChain() {
    Backup fullBackup =
        ModelFactory.createBackup(
            defaultCustomer.getUuid(), UUID.randomUUID(), s3StorageConfig.getConfigUUID());
    fullBackup.transitionState(BackupState.Completed);
    DeleteBackupYb.Params params = new DeleteBackupYb.Params();
    params.backupUUID = fullBackup.getBackupUUID();
    params.customerUUID = defaultCustomer.getUuid();
    Backup incrementalBackup1 =
        ModelFactory.createBackup(
            defaultCustomer.getUuid(), UUID.randomUUID(), s3StorageConfig.getConfigUUID());
    Backup incrementalBackup2 =
        ModelFactory.createBackup(
            defaultCustomer.getUuid(), UUID.randomUUID(), s3StorageConfig.getConfigUUID());
    incrementalBackup1.transitionState(BackupState.Completed);
    incrementalBackup1.setBaseBackupUUID(fullBackup.getBackupUUID());
    incrementalBackup1.save();
    incrementalBackup2.transitionState(BackupState.Completed);
    incrementalBackup2.setBaseBackupUUID(fullBackup.getBackupUUID());
    incrementalBackup2.save();
    DeleteBackupYb deleteBackupTask = AbstractTaskBase.createTask(DeleteBackupYb.class);
    deleteBackupTask.initialize(params);
    deleteBackupTask.run();
    fullBackup.refresh();
    assertEquals(BackupState.QueuedForDeletion, fullBackup.getState());
    incrementalBackup1.refresh();
    assertEquals(BackupState.QueuedForDeletion, incrementalBackup1.getState());
    incrementalBackup2.refresh();
    assertEquals(BackupState.QueuedForDeletion, incrementalBackup2.getState());
  }
}
