package com.yugabyte.yw.commissioner.tasks;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;

import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Backup.BackupState;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.configs.CustomerConfig.ConfigState;
import java.util.UUID;
import junitparams.JUnitParamsRunner;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(JUnitParamsRunner.class)
public class DeleteCustomerStorageConfigTest extends FakeDBApplication {

  private Customer defaultCustomer;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
  }

  @Test
  public void testCustomerConfigDeleteStateUpdateSuccess() {
    UUID universeUUID = UUID.randomUUID();
    CustomerConfig s3StorageConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST1");
    Backup backup =
        ModelFactory.createBackup(
            defaultCustomer.getUuid(), universeUUID, s3StorageConfig.getConfigUUID());
    backup.transitionState(BackupState.Completed);
    DeleteCustomerStorageConfig deleteCustomerStorageConfigTask =
        AbstractTaskBase.createTask(DeleteCustomerStorageConfig.class);
    DeleteCustomerStorageConfig.Params params = new DeleteCustomerStorageConfig.Params();
    params.customerUUID = defaultCustomer.getUuid();
    params.configUUID = s3StorageConfig.getConfigUUID();
    params.isDeleteBackups = true;
    deleteCustomerStorageConfigTask.initialize(params);
    deleteCustomerStorageConfigTask.run();
    backup.refresh();
    s3StorageConfig.refresh();
    assertEquals(BackupState.QueuedForDeletion, backup.getState());
    assertEquals(ConfigState.QueuedForDeletion, s3StorageConfig.getState());
  }

  @Test
  public void testCustomerConfigDeleteStateUpdateSuccessWithOutDeleteBackup() {
    UUID universeUUID = UUID.randomUUID();
    CustomerConfig s3StorageConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST2");
    Backup backup =
        ModelFactory.createBackup(
            defaultCustomer.getUuid(), universeUUID, s3StorageConfig.getConfigUUID());
    DeleteCustomerStorageConfig deleteCustomerStorageConfigTask =
        AbstractTaskBase.createTask(DeleteCustomerStorageConfig.class);
    DeleteCustomerStorageConfig.Params params = new DeleteCustomerStorageConfig.Params();
    params.customerUUID = defaultCustomer.getUuid();
    params.configUUID = s3StorageConfig.getConfigUUID();
    params.isDeleteBackups = false;
    deleteCustomerStorageConfigTask.initialize(params);
    deleteCustomerStorageConfigTask.run();
    s3StorageConfig.refresh();
    assertEquals(ConfigState.QueuedForDeletion, s3StorageConfig.getState());
  }

  @Test
  public void testCustomerConfigDeleteStateUpdateSuccessWithIsDeleteBackup() {
    UUID universeUUID = UUID.randomUUID();
    CustomerConfig s3StorageConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST2");
    Backup backup =
        ModelFactory.createBackup(
            defaultCustomer.getUuid(), universeUUID, s3StorageConfig.getConfigUUID());
    backup.transitionState(BackupState.Completed);
    DeleteCustomerStorageConfig deleteCustomerStorageConfigTask =
        AbstractTaskBase.createTask(DeleteCustomerStorageConfig.class);
    DeleteCustomerStorageConfig.Params params = new DeleteCustomerStorageConfig.Params();
    params.customerUUID = defaultCustomer.getUuid();
    params.configUUID = s3StorageConfig.getConfigUUID();
    params.isDeleteBackups = true;
    deleteCustomerStorageConfigTask.initialize(params);
    deleteCustomerStorageConfigTask.run();
    backup.refresh();
    s3StorageConfig.refresh();
    assertEquals(BackupState.QueuedForDeletion, backup.getState());
    assertEquals(ConfigState.QueuedForDeletion, s3StorageConfig.getState());
  }

  @Test
  public void testAssocicatedScheduleStop() {
    UUID universeUUID = UUID.randomUUID();
    CustomerConfig s3StorageConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST3");
    Schedule schedule =
        ModelFactory.createScheduleBackup(
            defaultCustomer.getUuid(), universeUUID, s3StorageConfig.getConfigUUID());
    Backup backup =
        ModelFactory.createBackup(
            defaultCustomer.getUuid(), universeUUID, s3StorageConfig.getConfigUUID());
    backup.transitionState(BackupState.Completed);
    DeleteCustomerStorageConfig deleteCustomerStorageConfigTask =
        AbstractTaskBase.createTask(DeleteCustomerStorageConfig.class);
    DeleteCustomerStorageConfig.Params params = new DeleteCustomerStorageConfig.Params();
    params.customerUUID = defaultCustomer.getUuid();
    params.configUUID = s3StorageConfig.getConfigUUID();
    params.isDeleteBackups = true;
    deleteCustomerStorageConfigTask.initialize(params);
    deleteCustomerStorageConfigTask.run();
    backup.refresh();
    s3StorageConfig.refresh();
    schedule.refresh();
    assertEquals(BackupState.QueuedForDeletion, backup.getState());
    assertEquals(ConfigState.QueuedForDeletion, s3StorageConfig.getState());
    assertEquals(Schedule.State.Stopped, schedule.getStatus());
  }

  @Test
  public void testDeleteInvalidCustomerConfig() {
    UUID invalidStorageConfigUUID = UUID.randomUUID();
    DeleteCustomerStorageConfig deleteCustomerStorageConfigTask =
        AbstractTaskBase.createTask(DeleteCustomerStorageConfig.class);
    DeleteCustomerStorageConfig.Params params = new DeleteCustomerStorageConfig.Params();
    params.customerUUID = defaultCustomer.getUuid();
    params.configUUID = invalidStorageConfigUUID;
    params.isDeleteBackups = true;
    deleteCustomerStorageConfigTask.initialize(params);
    RuntimeException re =
        assertThrows(RuntimeException.class, () -> deleteCustomerStorageConfigTask.run());
    assertEquals("Invalid StorageConfig UUID: " + invalidStorageConfigUUID, re.getMessage());
  }

  @Test
  public void testDeleteInUseStorageConfig() {
    UUID universeUUID = UUID.randomUUID();
    CustomerConfig s3StorageConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST2");
    Backup backup =
        ModelFactory.createBackup(
            defaultCustomer.getUuid(), universeUUID, s3StorageConfig.getConfigUUID());
    DeleteCustomerStorageConfig deleteCustomerStorageConfigTask =
        AbstractTaskBase.createTask(DeleteCustomerStorageConfig.class);
    DeleteCustomerStorageConfig.Params params = new DeleteCustomerStorageConfig.Params();
    params.customerUUID = defaultCustomer.getUuid();
    params.configUUID = s3StorageConfig.getConfigUUID();
    params.isDeleteBackups = true;
    deleteCustomerStorageConfigTask.initialize(params);
    assertThrows(RuntimeException.class, () -> deleteCustomerStorageConfigTask.run());
  }
}
