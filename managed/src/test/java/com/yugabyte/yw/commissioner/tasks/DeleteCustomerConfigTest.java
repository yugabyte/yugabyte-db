package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.models.TaskInfo.State.Failure;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerConfig;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class DeleteCustomerConfigTest extends CommissionerBaseTest {

  private Customer defaultCustomer;
  private Universe defaultUniverse;
  private Backup backup;
  private Schedule schedule;
  private CustomerConfig nfsStorageConfig;

  private TaskInfo submitTask(TaskType taskType, ITaskParams taskParams, boolean isSuccess) {
    TaskInfo taskInfo = null;
    try {
      UUID taskUUID = commissioner.submit(taskType, taskParams);
      waitForTask(taskUUID);
      taskInfo = TaskInfo.getOrBadRequest(taskUUID);
      if (isSuccess) {
        assertEquals(Success, taskInfo.getTaskState());
      } else {
        assertEquals(Failure, taskInfo.getTaskState());
      }
    } catch (Exception e) {
      assertNull(e.getMessage());
    }
    return taskInfo;
  }

  @Override
  @Before
  public void setUp() {
    super.setUp();
    defaultCustomer = ModelFactory.testCustomer();
    defaultUniverse = ModelFactory.createUniverse(defaultCustomer.getCustomerId());
    nfsStorageConfig = ModelFactory.createNfsStorageConfig(defaultCustomer, "TEST0");
    backup =
        ModelFactory.createBackup(
            defaultCustomer.uuid, defaultUniverse.universeUUID, nfsStorageConfig.configUUID);
    schedule =
        ModelFactory.createScheduleBackup(
            defaultCustomer.uuid, defaultUniverse.universeUUID, nfsStorageConfig.configUUID);
  }

  @Test
  public void testDeleteCustomerConfigWithBackups() throws InterruptedException {
    DeleteCustomerConfig.Params params = new DeleteCustomerConfig.Params();
    params.customerUUID = defaultCustomer.uuid;
    params.configUUID = nfsStorageConfig.configUUID;
    params.isDeleteBackups = true;
    BackupTableParams bParams = backup.getBackupInfo();
    bParams.actionType = BackupTableParams.ActionType.CREATE;
    backup.setBackupInfo(bParams);
    backup.transitionState(Backup.BackupState.Completed);
    ShellResponse shellResponse = new ShellResponse();
    shellResponse.message = "{\"success\": true}";
    shellResponse.code = 0;
    when(mockTableManager.deleteBackup(any())).thenReturn(shellResponse);
    submitTask(TaskType.DeleteCustomerConfig, params, true);
    verify(mockTableManager, times(1)).deleteBackup(any());
    // Backup state should be DELETED.
    backup = Backup.getOrBadRequest(defaultCustomer.uuid, backup.backupUUID);
    assertEquals(Backup.BackupState.Deleted, backup.state);
  }

  @Test
  public void testDeleteCustomerConfigWithoutBackups() throws InterruptedException {
    DeleteCustomerConfig.Params params = new DeleteCustomerConfig.Params();
    params.customerUUID = defaultCustomer.uuid;
    params.configUUID = nfsStorageConfig.configUUID;
    submitTask(TaskType.DeleteCustomerConfig, params, true);
    verify(mockTableManager, times(0)).deleteBackup(any());
  }

  @Test
  public void testDeleteCustomerConfigWithSchedules() {
    DeleteCustomerConfig.Params params = new DeleteCustomerConfig.Params();
    params.customerUUID = defaultCustomer.uuid;
    params.configUUID = nfsStorageConfig.configUUID;
    submitTask(TaskType.DeleteCustomerConfig, params, true);
    schedule = Schedule.getOrBadRequest(schedule.scheduleUUID);
    assertEquals(Schedule.State.Stopped, schedule.getStatus());
    verify(mockTableManager, times(0)).deleteBackup(any());
  }
}
