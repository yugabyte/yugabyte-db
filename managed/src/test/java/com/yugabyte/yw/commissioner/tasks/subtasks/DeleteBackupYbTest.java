// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;

import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerConfig;
import com.yugabyte.yw.models.Backup.BackupState;
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

  @Before
  public void setUp() {
    UUID universeUUID = UUID.randomUUID();
    defaultCustomer = ModelFactory.testCustomer();
    CustomerConfig s3StorageConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST1");
    backup =
        ModelFactory.createBackup(defaultCustomer.uuid, universeUUID, s3StorageConfig.configUUID);
  }

  @Test
  @Parameters({"InProgress", "DeleteInProgress", "QueuedForDeletion"})
  @TestCaseName("testFailueWithInValidStateWhenState:{0}")
  public void testFailueWithInValidState(BackupState state) {
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
}
