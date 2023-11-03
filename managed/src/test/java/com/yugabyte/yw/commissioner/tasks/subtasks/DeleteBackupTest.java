/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks.subtasks;

import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static com.yugabyte.yw.models.Backup.BackupState.Completed;
import static com.yugabyte.yw.models.Backup.BackupState.Failed;
import static com.yugabyte.yw.models.Backup.BackupState.FailedToDelete;
import static com.yugabyte.yw.models.Backup.BackupState.InProgress;
import static org.junit.Assert.assertEquals;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.common.AssertHelper;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.configs.CustomerConfig;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import play.mvc.Result;

@RunWith(MockitoJUnitRunner.class)
public class DeleteBackupTest extends FakeDBApplication {

  private Customer defaultCustomer;
  private Backup backup;

  @Before
  public void setUp() {
    UUID universeUUID = UUID.randomUUID();
    defaultCustomer = ModelFactory.testCustomer();
    CustomerConfig s3StorageConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST100");
    backup =
        ModelFactory.createBackup(
            defaultCustomer.getUuid(), universeUUID, s3StorageConfig.getConfigUUID());
  }

  // Test that only backups in Complete state or Failed state can be deleted.
  // Otherwise the run of backup task is a no-op
  @Test
  public void invalid() {
    assertEquals(InProgress, backup.getState());
    DeleteBackup.Params params = new DeleteBackup.Params();
    params.backupUUID = backup.getBackupUUID();
    params.customerUUID = defaultCustomer.getUuid();

    DeleteBackup deleteBackupTask = AbstractTaskBase.createTask(DeleteBackup.class);
    deleteBackupTask.initialize(params);
    deleteBackupTask.run();

    Backup backup = Backup.get(params.customerUUID, params.backupUUID);
    assertEquals(InProgress, backup.getState());
  }

  @Test
  public void successWithCompletedBackup() {
    backup.transitionState(Completed);
    DeleteBackup.Params params = new DeleteBackup.Params();
    params.backupUUID = backup.getBackupUUID();
    params.customerUUID = defaultCustomer.getUuid();

    ShellResponse shellResponse = new ShellResponse();
    shellResponse.message = "{\"success\": true}";
    shellResponse.code = 0;
    when(mockTableManager.deleteBackup(any())).thenReturn(shellResponse);

    DeleteBackup deleteBackupTask = AbstractTaskBase.createTask(DeleteBackup.class);
    deleteBackupTask.initialize(params);
    deleteBackupTask.run();

    verify(mockTableManager, times(1)).deleteBackup(any());
    Result result =
        assertPlatformException(
            () -> Backup.getOrBadRequest(params.customerUUID, params.backupUUID));
    AssertHelper.assertBadRequest(result, "Invalid customer or backup UUID");
  }

  @Test
  public void successWithFailedBackup() {
    backup.transitionState(Failed);
    DeleteBackup.Params params = new DeleteBackup.Params();
    params.backupUUID = backup.getBackupUUID();
    params.customerUUID = defaultCustomer.getUuid();

    ShellResponse shellResponse = new ShellResponse();
    shellResponse.message = "{\"success\": true}";
    shellResponse.code = 0;
    when(mockTableManager.deleteBackup(any())).thenReturn(shellResponse);

    DeleteBackup deleteBackupTask = AbstractTaskBase.createTask(DeleteBackup.class);
    deleteBackupTask.initialize(params);
    deleteBackupTask.run();

    verify(mockTableManager, times(1)).deleteBackup(any());
    Result result =
        assertPlatformException(
            () -> Backup.getOrBadRequest(params.customerUUID, params.backupUUID));
    AssertHelper.assertBadRequest(result, "Invalid customer or backup UUID");
  }

  @Test
  public void failureWithCompletedBackup() {
    backup.transitionState(Completed);
    DeleteBackup.Params params = new DeleteBackup.Params();
    params.backupUUID = backup.getBackupUUID();
    params.customerUUID = defaultCustomer.getUuid();

    ShellResponse shellResponse = new ShellResponse();
    shellResponse.message = "{\"success\": false}";
    shellResponse.code = 22;
    when(mockTableManager.deleteBackup(any())).thenReturn(shellResponse);

    DeleteBackup deleteBackupTask = AbstractTaskBase.createTask(DeleteBackup.class);
    deleteBackupTask.initialize(params);
    deleteBackupTask.run();

    verify(mockTableManager, times(1)).deleteBackup(any());
    Backup backup = Backup.get(params.customerUUID, params.backupUUID);
    assertEquals(FailedToDelete, backup.getState());
  }

  @Test
  public void failureWithFailedBackup() {
    backup.transitionState(Failed);
    DeleteBackup.Params params = new DeleteBackup.Params();
    params.backupUUID = backup.getBackupUUID();
    params.customerUUID = defaultCustomer.getUuid();

    ShellResponse shellResponse = new ShellResponse();
    shellResponse.message = "{\"success\": false}";
    shellResponse.code = 22;
    when(mockTableManager.deleteBackup(any())).thenReturn(shellResponse);

    DeleteBackup deleteBackupTask = AbstractTaskBase.createTask(DeleteBackup.class);
    deleteBackupTask.initialize(params);
    deleteBackupTask.run();

    verify(mockTableManager, times(1)).deleteBackup(any());
    Backup backup = Backup.get(params.customerUUID, params.backupUUID);
    assertEquals(FailedToDelete, backup.getState());
  }

  @Test
  public void unexpectedException() {
    backup.transitionState(Completed);
    DeleteBackup.Params params = new DeleteBackup.Params();
    params.backupUUID = backup.getBackupUUID();
    params.customerUUID = defaultCustomer.getUuid();

    ShellResponse shellResponse = new ShellResponse();
    shellResponse.message = "{\"success\": false}";
    shellResponse.code = 22;
    when(mockTableManager.deleteBackup(any())).thenThrow(new RuntimeException("expected"));

    DeleteBackup deleteBackupTask = AbstractTaskBase.createTask(DeleteBackup.class);
    deleteBackupTask.initialize(params);
    deleteBackupTask.run();

    verify(mockTableManager, times(1)).deleteBackup(any());
    Backup backup = Backup.get(params.customerUUID, params.backupUUID);
    assertEquals(FailedToDelete, backup.getState());
  }
}
