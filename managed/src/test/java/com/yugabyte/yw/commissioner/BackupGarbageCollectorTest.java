// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.doThrow;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.common.BackupUtil;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.TableManagerYb;
import com.yugabyte.yw.common.YbcManager;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.customer.config.CustomerConfigService;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Backup.BackupState;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.configs.CustomerConfig.ConfigState;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class BackupGarbageCollectorTest extends FakeDBApplication {

  @Mock PlatformScheduler mockPlatformScheduler;

  @Mock RuntimeConfigFactory mockRuntimeConfigFactory;

  private Customer defaultCustomer;
  private Universe defaultUniverse;
  private BackupGarbageCollector backupGC;
  private CustomerConfigService customerConfigService;
  private TableManagerYb tableManagerYb;
  private BackupUtil mockBackupUtil;
  private YbcManager mockYbcManager;
  private CustomerConfig s3StorageConfig;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    defaultUniverse = ModelFactory.createUniverse(defaultCustomer.getCustomerId());
    customerConfigService = app.injector().instanceOf(CustomerConfigService.class);
    tableManagerYb = app.injector().instanceOf(TableManagerYb.class);
    mockBackupUtil = mock(BackupUtil.class);
    s3StorageConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST0");
    backupGC =
        new BackupGarbageCollector(
            mockPlatformScheduler,
            customerConfigService,
            mockRuntimeConfigFactory,
            tableManagerYb,
            mockBackupUtil,
            mockYbcManager,
            mockTaskManager,
            mockCommissioner);
  }

  @Test
  public void testDeleteAWSBackupSuccess() {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST1");
    BackupTableParams bp = new BackupTableParams();
    bp.storageConfigUUID = customerConfig.configUUID;
    bp.universeUUID = UUID.randomUUID();
    Backup backup = Backup.create(defaultCustomer.uuid, bp);
    backup.transitionState(BackupState.QueuedForDeletion);
    List<String> backupLocations = new ArrayList<>();
    backupLocations.add(backup.getBackupInfo().storageLocation);
    when(mockBackupUtil.getBackupLocations(backup)).thenReturn(backupLocations);
    backupGC.scheduleRunner();
    assertThrows(
        PlatformServiceException.class,
        () -> Backup.getOrBadRequest(defaultCustomer.uuid, backup.backupUUID));
  }

  @Test
  public void testDeleteGCSBackupSuccess() {
    CustomerConfig customerConfig = ModelFactory.createGcsStorageConfig(defaultCustomer, "TEST2");
    BackupTableParams bp = new BackupTableParams();
    bp.storageConfigUUID = customerConfig.configUUID;
    bp.universeUUID = UUID.randomUUID();
    Backup backup = Backup.create(defaultCustomer.uuid, bp);
    backup.transitionState(BackupState.QueuedForDeletion);
    List<String> backupLocations = new ArrayList<>();
    backupLocations.add(backup.getBackupInfo().storageLocation);
    when(mockBackupUtil.getBackupLocations(backup)).thenReturn(backupLocations);
    backupGC.scheduleRunner();
    assertThrows(
        PlatformServiceException.class,
        () -> Backup.getOrBadRequest(defaultCustomer.uuid, backup.backupUUID));
  }

  @Test
  public void testDeleteAZBackupSuccess() {
    CustomerConfig customerConfig = ModelFactory.createAZStorageConfig(defaultCustomer, "TEST3");
    BackupTableParams bp = new BackupTableParams();
    bp.storageConfigUUID = customerConfig.configUUID;
    bp.universeUUID = UUID.randomUUID();
    Backup backup = Backup.create(defaultCustomer.uuid, bp);
    backup.transitionState(BackupState.QueuedForDeletion);
    List<String> backupLocations = new ArrayList<>();
    backupLocations.add(backup.getBackupInfo().storageLocation);
    when(mockBackupUtil.getBackupLocations(backup)).thenReturn(backupLocations);
    backupGC.scheduleRunner();
    assertThrows(
        PlatformServiceException.class,
        () -> Backup.getOrBadRequest(defaultCustomer.uuid, backup.backupUUID));
  }

  @Test
  public void testDeleteNFSBackupSuccess() {
    CustomerConfig customerConfig = ModelFactory.createNfsStorageConfig(defaultCustomer, "TEST4");
    BackupTableParams bp = new BackupTableParams();
    bp.storageConfigUUID = customerConfig.configUUID;
    bp.universeUUID = defaultUniverse.universeUUID;
    Backup backup = Backup.create(defaultCustomer.uuid, bp);
    backup.transitionState(BackupState.QueuedForDeletion);
    ShellResponse shellResponse = new ShellResponse();
    shellResponse.message = "{\"success\": true}";
    shellResponse.code = 0;
    when(mockTableManagerYb.deleteBackup(any())).thenReturn(shellResponse);
    backupGC.scheduleRunner();
    assertThrows(
        PlatformServiceException.class,
        () -> Backup.getOrBadRequest(defaultCustomer.uuid, backup.backupUUID));
  }

  @Test
  public void testDeleteNFSBackupSuccessWithUniverseDeleted() {
    CustomerConfig customerConfig = ModelFactory.createNfsStorageConfig(defaultCustomer, "TEST5");
    BackupTableParams bp = new BackupTableParams();
    bp.storageConfigUUID = customerConfig.configUUID;
    bp.universeUUID = UUID.randomUUID();
    Backup backup = Backup.create(defaultCustomer.uuid, bp);
    backup.transitionState(BackupState.QueuedForDeletion);
    defaultUniverse.delete();
    backupGC.scheduleRunner();
    assertThrows(
        PlatformServiceException.class,
        () -> Backup.getOrBadRequest(defaultCustomer.uuid, backup.backupUUID));
  }

  @Test
  public void testDeleteBackupFailureWithInvalidCredentials() {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST6");
    BackupTableParams bp = new BackupTableParams();
    bp.storageConfigUUID = customerConfig.configUUID;
    bp.universeUUID = UUID.randomUUID();
    Backup backup = Backup.create(defaultCustomer.uuid, bp);
    backup.transitionState(BackupState.QueuedForDeletion);
    doThrow(new PlatformServiceException(BAD_REQUEST, "error"))
        .when(mockBackupUtil)
        .validateStorageConfig(any());
    backupGC.scheduleRunner();
    backup = Backup.getOrBadRequest(defaultCustomer.uuid, backup.backupUUID);
    assertEquals(BackupState.FailedToDelete, backup.state);
  }

  @Test
  public void testDeleteNFSBackupFailure() {
    CustomerConfig customerConfig = ModelFactory.createNfsStorageConfig(defaultCustomer, "TEST7");
    BackupTableParams bp = new BackupTableParams();
    bp.storageConfigUUID = customerConfig.configUUID;
    bp.universeUUID = defaultUniverse.universeUUID;
    Backup backup = Backup.create(defaultCustomer.uuid, bp);
    backup.transitionState(BackupState.QueuedForDeletion);
    ShellResponse shellResponse = new ShellResponse();
    shellResponse.message = "{\"error\": true}";
    shellResponse.code = 2;
    when(mockTableManagerYb.deleteBackup(any())).thenReturn(shellResponse);
    backupGC.scheduleRunner();
    backup = Backup.getOrBadRequest(defaultCustomer.uuid, backup.backupUUID);
    assertEquals(BackupState.FailedToDelete, backup.state);
  }

  @Test
  public void testDeleteCloudBackupFailure() throws Exception {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST8");
    BackupTableParams bp = new BackupTableParams();
    bp.storageConfigUUID = customerConfig.configUUID;
    bp.universeUUID = defaultUniverse.universeUUID;
    Backup backup = Backup.create(defaultCustomer.uuid, bp);
    backup.transitionState(BackupState.QueuedForDeletion);
    List<String> backupLocations = new ArrayList<>();
    backupLocations.add(backup.getBackupInfo().storageLocation);
    when(mockBackupUtil.getBackupLocations(backup)).thenReturn(backupLocations);
    doThrow(new RuntimeException()).when(mockAWSUtil).deleteKeyIfExists(any(), any());
    backupGC.scheduleRunner();
    backup = Backup.getOrBadRequest(defaultCustomer.uuid, backup.backupUUID);
    assertEquals(BackupState.FailedToDelete, backup.state);
  }

  @Test
  public void testDeleteBackupWithInvalidStorageConfig() {
    BackupTableParams bp = new BackupTableParams();
    bp.storageConfigUUID = UUID.randomUUID();
    bp.universeUUID = UUID.randomUUID();
    Backup backup = Backup.create(defaultCustomer.uuid, bp);
    backup.transitionState(BackupState.QueuedForDeletion);
    backupGC.scheduleRunner();
    backup = Backup.getOrBadRequest(defaultCustomer.uuid, backup.backupUUID);
    assertEquals(BackupState.FailedToDelete, backup.state);
  }

  @Test
  public void testDeleteCustomerConfigSuccess() {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST9");
    BackupTableParams bp = new BackupTableParams();
    bp.storageConfigUUID = customerConfig.configUUID;
    bp.universeUUID = defaultUniverse.universeUUID;
    Backup backup = Backup.create(defaultCustomer.uuid, bp);
    backup.transitionState(BackupState.QueuedForDeletion);
    customerConfig.setState(ConfigState.QueuedForDeletion);
    List<String> backupLocations = new ArrayList<>();
    backupLocations.add(backup.getBackupInfo().storageLocation);
    when(mockBackupUtil.getBackupLocations(backup)).thenReturn(backupLocations);
    backupGC.scheduleRunner();
    assertThrows(
        PlatformServiceException.class,
        () -> Backup.getOrBadRequest(defaultCustomer.uuid, backup.backupUUID));
    assertThrows(
        PlatformServiceException.class,
        () ->
            customerConfigService.getOrBadRequest(defaultCustomer.uuid, customerConfig.configUUID));
  }

  @Test
  public void testDeleteCustomerConfigSuccessWithBackupsLeft() {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST10");
    BackupTableParams bp = new BackupTableParams();
    bp.storageConfigUUID = customerConfig.configUUID;
    bp.universeUUID = defaultUniverse.universeUUID;
    Backup backup = Backup.create(defaultCustomer.uuid, bp);
    backup.transitionState(BackupState.Completed);
    customerConfig.setState(ConfigState.QueuedForDeletion);
    backupGC.scheduleRunner();
    assertThrows(
        PlatformServiceException.class,
        () ->
            customerConfigService.getOrBadRequest(defaultCustomer.uuid, customerConfig.configUUID));
  }

  @Test
  public void testDeleteCustomerConfigSuccessWithBackupsError() {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST11");
    BackupTableParams bp = new BackupTableParams();
    bp.storageConfigUUID = customerConfig.configUUID;
    bp.universeUUID = defaultUniverse.universeUUID;
    Backup backup = Backup.create(defaultCustomer.uuid, bp);
    backup.transitionState(BackupState.QueuedForDeletion);
    doThrow(new PlatformServiceException(BAD_REQUEST, "error"))
        .when(mockBackupUtil)
        .validateStorageConfig(any());
    customerConfig.setState(ConfigState.QueuedForDeletion);
    backupGC.scheduleRunner();
    assertThrows(
        PlatformServiceException.class,
        () ->
            customerConfigService.getOrBadRequest(defaultCustomer.uuid, customerConfig.configUUID));
    backup.refresh();
    assertEquals(BackupState.FailedToDelete, backup.state);
  }

  @Test
  public void testDeleteExpiredBackups() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    Backup backup =
        ModelFactory.createBackupWithExpiry(
            defaultCustomer.uuid, defaultUniverse.universeUUID, s3StorageConfig.configUUID);
    backup.transitionState(Backup.BackupState.Completed);

    // Test that we do not delete backups of paused universe
    setUniversePaused(true, defaultUniverse);
    backupGC.scheduleRunner();
    assertEquals(0, Backup.getExpiredBackups().get(defaultCustomer).size());
    assertEquals(null, CustomerTask.get(defaultCustomer.uuid, fakeTaskUUID));
    verify(mockCommissioner, times(0)).submit(any(), any());

    // Unpause the universe and make sure that we will delete the backup.
    setUniversePaused(false, defaultUniverse);
    backupGC.scheduleRunner();
    CustomerTask task = CustomerTask.get(defaultCustomer.uuid, fakeTaskUUID);
    assertEquals(1, Backup.getExpiredBackups().get(defaultCustomer).size());
    assertEquals(CustomerTask.TaskType.Delete, task.getType());
    verify(mockCommissioner, times(1)).submit(any(), any());
  }

  @Test
  public void testDeleteExpiredChildIncrementalBackup() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);

    Backup backup =
        ModelFactory.createBackupWithExpiry(
            defaultCustomer.uuid, defaultUniverse.universeUUID, s3StorageConfig.configUUID);
    backup.transitionState(Backup.BackupState.Completed);

    Backup backup2 =
        ModelFactory.createBackupWithExpiry(
            defaultCustomer.uuid, defaultUniverse.universeUUID, s3StorageConfig.configUUID);
    backup2.transitionState(Backup.BackupState.Completed);
    backup2.baseBackupUUID = UUID.randomUUID();
    backup2.save();

    backupGC.scheduleRunner();
    CustomerTask task = CustomerTask.get(defaultCustomer.uuid, fakeTaskUUID);
    assertEquals(1, Backup.getExpiredBackups().get(defaultCustomer).size());
    assertEquals(CustomerTask.TaskType.Delete, task.getType());
    verify(mockCommissioner, times(1)).submit(any(), any());

    backup2.baseBackupUUID = backup2.backupUUID;
    backup2.save();
    assertEquals(2, Backup.getExpiredBackups().get(defaultCustomer).size());
    backupGC.scheduleRunner();
    verify(mockCommissioner, times(3)).submit(any(), any());
  }

  @Test
  public void testDeleteExpiredBackups_universeDeleted() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);

    Backup backup =
        ModelFactory.createBackupWithExpiry(
            defaultCustomer.uuid, defaultUniverse.universeUUID, s3StorageConfig.configUUID);
    backup.transitionState(Backup.BackupState.Completed);
    defaultUniverse.delete();
    backupGC.scheduleRunner();

    CustomerTask task = CustomerTask.get(defaultCustomer.uuid, fakeTaskUUID);
    assertEquals(1, Backup.getExpiredBackups().get(defaultCustomer).size());
    assertEquals(CustomerTask.TaskType.Delete, task.getType());
    verify(mockCommissioner, times(1)).submit(any(), any());
    assertEquals(1, Backup.getExpiredBackups().get(defaultCustomer).size());
  }

  @Test
  public void testDeleteExpiredBackupsCreatedFromSchedule() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    Schedule s =
        ModelFactory.createScheduleBackup(
            defaultCustomer.uuid, defaultUniverse.universeUUID, s3StorageConfig.configUUID);
    UUID fakeScheduleUUID = s.getScheduleUUID();
    for (int i = 0; i < 5; i++) {
      Backup backup =
          ModelFactory.createExpiredBackupWithScheduleUUID(
              defaultCustomer.uuid,
              defaultUniverse.universeUUID,
              s3StorageConfig.configUUID,
              fakeScheduleUUID);
      backup.transitionState(Backup.BackupState.Completed);
    }
    for (int i = 0; i < 2; i++) {
      Backup backup =
          ModelFactory.createBackupWithExpiry(
              defaultCustomer.uuid, defaultUniverse.universeUUID, s3StorageConfig.configUUID);
      backup.transitionState(Backup.BackupState.Completed);
    }
    backupGC.scheduleRunner();
    assertEquals(7, Backup.getExpiredBackups().get(defaultCustomer).size());

    // 4 times for deleting expired backups
    verify(mockCommissioner, times(4)).submit(any(), any());
  }

  @Test
  public void testDeleteExpiredBackupsCreatedFromDeletedSchedule() {
    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    UUID fakeScheduleUUID = UUID.randomUUID();
    for (int i = 0; i < 5; i++) {
      Backup backup =
          ModelFactory.createExpiredBackupWithScheduleUUID(
              defaultCustomer.uuid,
              defaultUniverse.universeUUID,
              s3StorageConfig.configUUID,
              fakeScheduleUUID);
      backup.transitionState(Backup.BackupState.Completed);
    }
    for (int i = 0; i < 2; i++) {
      Backup backup =
          ModelFactory.createBackupWithExpiry(
              defaultCustomer.uuid, defaultUniverse.universeUUID, s3StorageConfig.configUUID);
      backup.transitionState(Backup.BackupState.Completed);
    }
    backupGC.scheduleRunner();
    assertEquals(7, Backup.getExpiredBackups().get(defaultCustomer).size());

    // 2 time for independent and 2 times from deleted scheduled expired backups.
    verify(mockCommissioner, times(4)).submit(any(), any());
  }

  @Test
  public void testSkipAlreadyRunningDeleteBackupTask() {
    Backup backup =
        ModelFactory.createBackupWithExpiry(
            defaultCustomer.uuid, defaultUniverse.universeUUID, s3StorageConfig.configUUID);
    backup.transitionState(Backup.BackupState.Completed);

    UUID fakeTaskUUID = UUID.randomUUID();
    when(mockTaskManager.isDeleteBackupTaskAlreadyPresent(defaultCustomer.uuid, backup.backupUUID))
        .thenReturn(true);
    backupGC.scheduleRunner();
    assertEquals(1, Backup.getExpiredBackups().get(defaultCustomer).size());
    assertEquals(null, CustomerTask.get(defaultCustomer.uuid, fakeTaskUUID));
    verify(mockCommissioner, times(0)).submit(any(), any());

    when(mockTaskManager.isDeleteBackupTaskAlreadyPresent(defaultCustomer.uuid, backup.backupUUID))
        .thenReturn(false);
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    backupGC.scheduleRunner();
    CustomerTask task = CustomerTask.get(defaultCustomer.uuid, fakeTaskUUID);
    assertEquals(1, Backup.getExpiredBackups().get(defaultCustomer).size());
    assertEquals(CustomerTask.TaskType.Delete, task.getType());
    verify(mockCommissioner, times(1)).submit(any(), any());
  }

  public static void setUniversePaused(boolean value, Universe universe) {
    Universe.UniverseUpdater updater =
        new Universe.UniverseUpdater() {
          @Override
          public void run(Universe universe) {
            UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
            universeDetails.universePaused = value;
            universe.setUniverseDetails(universeDetails);
          }
        };
    Universe.saveDetails(universe.universeUUID, updater);
  }
}
