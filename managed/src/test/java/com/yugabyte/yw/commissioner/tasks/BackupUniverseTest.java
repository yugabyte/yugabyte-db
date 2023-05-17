// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.models.Backup.BackupState.Completed;
import static com.yugabyte.yw.models.Backup.BackupState.Failed;
import static com.yugabyte.yw.models.TaskInfo.State.Failure;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.HashMap;
import java.util.Map;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class BackupUniverseTest extends CommissionerBaseTest {

  private Universe defaultUniverse;

  private Users defaultUser;

  @Override
  @Before
  public void setUp() {
    super.setUp();
    defaultCustomer = ModelFactory.testCustomer();
    defaultUniverse = ModelFactory.createUniverse();
    Map<String, String> config = new HashMap<>();
    config.put(Universe.TAKE_BACKUPS, "true");
    defaultUniverse.updateConfig(config);
    defaultUniverse.save();
    defaultUser = ModelFactory.testUser(defaultCustomer);
  }

  private TaskInfo submitTask(BackupTableParams.ActionType actionType, boolean enableVerboseLogs) {
    BackupTableParams backupTableParams = new BackupTableParams();
    backupTableParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    backupTableParams.setTableName("bar");
    backupTableParams.setKeyspace("foo");
    backupTableParams.tableUUID = UUID.randomUUID();
    backupTableParams.storageConfigUUID = UUID.randomUUID();
    backupTableParams.actionType = actionType;
    backupTableParams.enableVerboseLogs = enableVerboseLogs;
    backupTableParams.customerUuid = defaultCustomer.getUuid();
    // Set http context
    TestUtils.setFakeHttpContext(defaultUser);

    try {
      UUID taskUUID = commissioner.submit(TaskType.BackupUniverse, backupTableParams);
      CustomerTask.create(
          defaultCustomer,
          defaultUniverse.getUniverseUUID(),
          taskUUID,
          CustomerTask.TargetType.Backup,
          CustomerTask.TaskType.Create,
          "bar");
      return waitForTask(taskUUID);
    } catch (InterruptedException e) {
      assertNull(e.getMessage());
    }
    return null;
  }

  @Test
  public void testBackupTableCreateAction() {
    ShellResponse shellResponse = new ShellResponse();
    shellResponse.message = "{\"snapshot_url\": \"/tmp/backup\", \"backup_size_in_bytes\": 340}";
    shellResponse.code = 0;
    when(mockTableManager.createBackup(any())).thenReturn(shellResponse);

    TaskInfo taskInfo = submitTask(BackupTableParams.ActionType.CREATE, false);
    verify(mockTableManager, times(1)).createBackup(any());
    assertEquals(Success, taskInfo.getTaskState());
    Backup backup = Backup.fetchAllBackupsByTaskUUID(taskInfo.getTaskUUID()).get(0);
    assertNotNull(backup);
    assertEquals(Completed, backup.getState());
  }

  @Test
  public void testBackupTableError() {
    ShellResponse shellResponse = new ShellResponse();
    shellResponse.message = "{\"error\": true}";
    shellResponse.code = 0;
    when(mockTableManager.createBackup(any())).thenReturn(shellResponse);

    TaskInfo taskInfo = submitTask(BackupTableParams.ActionType.CREATE, true);
    assertEquals(Failure, taskInfo.getTaskState());
    verify(mockTableManager, times(1)).createBackup(any());
    Backup backup = Backup.fetchAllBackupsByTaskUUID(taskInfo.getTaskUUID()).get(0);
    assertNotNull(backup);
    assertEquals(Failed, backup.getState());
  }

  @Test
  public void testBackupTableFatal() {
    ShellResponse shellResponse = new ShellResponse();
    shellResponse.message = "{\"error\": true}";
    shellResponse.code = 99;
    when(mockTableManager.createBackup(any())).thenReturn(shellResponse);
    TaskInfo taskInfo = submitTask(BackupTableParams.ActionType.CREATE, true);
    assertEquals(Failure, taskInfo.getTaskState());
    verify(mockTableManager, times(1)).createBackup(any());
    Backup backup = Backup.fetchAllBackupsByTaskUUID(taskInfo.getTaskUUID()).get(0);
    assertNotNull(backup);
    assertEquals(Failed, backup.getState());
  }

  @Test
  public void testBackupTableRestoreAction() {
    ShellResponse shellResponse = new ShellResponse();
    shellResponse.message = "{\"success\": true}";
    shellResponse.code = 0;
    when(mockTableManager.createBackup(any())).thenReturn(shellResponse);

    TaskInfo taskInfo = submitTask(BackupTableParams.ActionType.RESTORE, false);
    verify(mockTableManager, times(1)).createBackup(any());
    assertEquals(Success, taskInfo.getTaskState());
    Backup backup = Backup.fetchAllBackupsByTaskUUID(taskInfo.getTaskUUID()).get(0);
    assertNotNull(backup);
    assertEquals(Completed, backup.getState());
  }

  @Test
  public void testBackupTableRestoreVerbose() {
    ShellResponse shellResponse = new ShellResponse();
    shellResponse.message = "{\"snapshot_url\": \"s3://random\", \"skipthis\": \"INFO\"}";
    shellResponse.code = 0;
    when(mockTableManager.createBackup(any())).thenReturn(shellResponse);

    TaskInfo taskInfo = submitTask(BackupTableParams.ActionType.RESTORE, true);
    verify(mockTableManager, times(1)).createBackup(any());
    assertEquals(Success, taskInfo.getTaskState());
    Backup backup = Backup.fetchAllBackupsByTaskUUID(taskInfo.getTaskUUID()).get(0);
    assertNotNull(backup);
    assertEquals(Completed, backup.getState());
  }

  @Test
  public void testBackupTableInvalidAction() {
    TaskInfo taskInfo = submitTask(null, false);
    assertEquals(Failure, taskInfo.getTaskState());
    verify(mockTableManager, times(0)).createBackup(any());
  }
}
