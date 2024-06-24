// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.doThrow;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.TableManagerYb;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.common.backuprestore.BackupHelper;
import com.yugabyte.yw.common.backuprestore.ybc.YbcManager;
import com.yugabyte.yw.common.config.CustomerConfKeys;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.customer.config.CustomerConfigService;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Backup.BackupState;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.configs.CustomerConfig.ConfigState;
import com.yugabyte.yw.models.helpers.TaskType;
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

  @Mock private RuntimeConfGetter mockConfGetter;

  private Customer defaultCustomer;
  private Universe defaultUniverse;
  private BackupGarbageCollector backupGC;
  private CustomerConfigService customerConfigService;
  private TableManagerYb tableManagerYb;
  private BackupHelper mockBackupHelper;
  private YbcManager mockYbcManager;
  private CustomerConfig s3StorageConfig;
  private Users defaultUser;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    defaultUniverse = ModelFactory.createUniverse(defaultCustomer.getId());
    customerConfigService = app.injector().instanceOf(CustomerConfigService.class);
    tableManagerYb = app.injector().instanceOf(TableManagerYb.class);
    mockBackupHelper = mock(BackupHelper.class);
    s3StorageConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST0");
    defaultUser = ModelFactory.testUser(defaultCustomer);
    backupGC =
        new BackupGarbageCollector(
            mockPlatformScheduler,
            customerConfigService,
            mockConfGetter,
            tableManagerYb,
            mockBackupHelper,
            mockYbcManager,
            mockTaskManager,
            mockCommissioner,
            mockStorageUtilFactory);
    when(mockConfGetter.getGlobalConf(eq(GlobalConfKeys.deleteExpiredBackupMaxGCSize)))
        .thenReturn(10);
    when(mockConfGetter.getConfForScope(defaultCustomer, CustomerConfKeys.backupGcNumberOfRetries))
        .thenReturn(1);
  }

  @Test
  public void testDeleteAWSBackupSuccess() {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST1");
    BackupTableParams bp = new BackupTableParams();
    bp.storageConfigUUID = customerConfig.getConfigUUID();
    bp.setUniverseUUID(UUID.randomUUID());
    Backup backup = Backup.create(defaultCustomer.getUuid(), bp);
    backup.transitionState(BackupState.QueuedForDeletion);
    List<String> backupLocations = new ArrayList<>();
    backupLocations.add(backup.getBackupInfo().storageLocation);
    when(mockStorageUtilFactory.getCloudUtil(anyString())).thenReturn(mockAWSUtil);
    when(mockAWSUtil.deleteKeyIfExists(any(), anyString())).thenReturn(true);
    when(mockAWSUtil.deleteStorage(any(), any())).thenReturn(true);
    backupGC.scheduleRunner();
    assertThrows(
        PlatformServiceException.class,
        () -> Backup.getOrBadRequest(defaultCustomer.getUuid(), backup.getBackupUUID()));
  }

  @Test
  public void testDeleteGCSBackupSuccess() {
    CustomerConfig customerConfig = ModelFactory.createGcsStorageConfig(defaultCustomer, "TEST2");
    BackupTableParams bp = new BackupTableParams();
    bp.storageConfigUUID = customerConfig.getConfigUUID();
    bp.setUniverseUUID(UUID.randomUUID());
    Backup backup = Backup.create(defaultCustomer.getUuid(), bp);
    backup.transitionState(BackupState.QueuedForDeletion);
    List<String> backupLocations = new ArrayList<>();
    when(mockStorageUtilFactory.getCloudUtil(anyString())).thenReturn(mockGCPUtil);
    backupLocations.add(backup.getBackupInfo().storageLocation);
    when(mockGCPUtil.deleteKeyIfExists(any(), anyString())).thenReturn(true);
    when(mockGCPUtil.deleteStorage(any(), any())).thenReturn(true);
    backupGC.scheduleRunner();
    assertThrows(
        PlatformServiceException.class,
        () -> Backup.getOrBadRequest(defaultCustomer.getUuid(), backup.getBackupUUID()));
  }

  @Test
  public void testDeleteAZBackupSuccess() {
    CustomerConfig customerConfig = ModelFactory.createAZStorageConfig(defaultCustomer, "TEST3");
    BackupTableParams bp = new BackupTableParams();
    bp.storageConfigUUID = customerConfig.getConfigUUID();
    bp.setUniverseUUID(UUID.randomUUID());
    Backup backup = Backup.create(defaultCustomer.getUuid(), bp);
    backup.transitionState(BackupState.QueuedForDeletion);
    List<String> backupLocations = new ArrayList<>();
    when(mockStorageUtilFactory.getCloudUtil(anyString())).thenReturn(mockAZUtil);
    backupLocations.add(backup.getBackupInfo().storageLocation);
    when(mockAZUtil.deleteKeyIfExists(any(), anyString())).thenReturn(true);
    when(mockAZUtil.deleteStorage(any(), any())).thenReturn(true);
    backupGC.scheduleRunner();
    assertThrows(
        PlatformServiceException.class,
        () -> Backup.getOrBadRequest(defaultCustomer.getUuid(), backup.getBackupUUID()));
  }

  @Test
  public void testDeleteNFSBackupSuccess() {
    CustomerConfig customerConfig = ModelFactory.createNfsStorageConfig(defaultCustomer, "TEST4");
    BackupTableParams bp = new BackupTableParams();
    bp.storageConfigUUID = customerConfig.getConfigUUID();
    bp.setUniverseUUID(defaultUniverse.getUniverseUUID());
    Backup backup = Backup.create(defaultCustomer.getUuid(), bp);
    backup.transitionState(BackupState.QueuedForDeletion);
    when(mockStorageUtilFactory.getStorageUtil(anyString())).thenReturn(mockNfsUtil);
    ShellResponse shellResponse = new ShellResponse();
    shellResponse.message = "{\"success\": true}";
    shellResponse.code = 0;
    when(mockTableManagerYb.deleteBackup(any())).thenReturn(shellResponse);
    backupGC.scheduleRunner();
    assertThrows(
        PlatformServiceException.class,
        () -> Backup.getOrBadRequest(defaultCustomer.getUuid(), backup.getBackupUUID()));
  }

  @Test
  public void testDeleteNFSBackupSuccessWithUniverseDeleted() {
    CustomerConfig customerConfig = ModelFactory.createNfsStorageConfig(defaultCustomer, "TEST5");
    BackupTableParams bp = new BackupTableParams();
    bp.storageConfigUUID = customerConfig.getConfigUUID();
    bp.setUniverseUUID(UUID.randomUUID());
    Backup backup = Backup.create(defaultCustomer.getUuid(), bp);
    backup.transitionState(BackupState.QueuedForDeletion);
    defaultUniverse.delete();
    backupGC.scheduleRunner();
    assertThrows(
        PlatformServiceException.class,
        () -> Backup.getOrBadRequest(defaultCustomer.getUuid(), backup.getBackupUUID()));
  }

  @Test
  public void testDeleteBackupFailureWithInvalidCredentials() {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST6");
    BackupTableParams bp = new BackupTableParams();
    bp.storageConfigUUID = customerConfig.getConfigUUID();
    bp.setUniverseUUID(UUID.randomUUID());
    Backup backup = Backup.create(defaultCustomer.getUuid(), bp);
    backup.transitionState(BackupState.QueuedForDeletion);
    doThrow(new PlatformServiceException(BAD_REQUEST, "error"))
        .when(mockBackupHelper)
        .validateStorageConfig(any());
    backupGC.scheduleRunner();
    backup = Backup.getOrBadRequest(defaultCustomer.getUuid(), backup.getBackupUUID());
    assertEquals(BackupState.FailedToDelete, backup.getState());
  }

  @Test
  public void testDeleteNFSBackupFailure() {
    CustomerConfig customerConfig = ModelFactory.createNfsStorageConfig(defaultCustomer, "TEST7");
    BackupTableParams bp = new BackupTableParams();
    bp.storageConfigUUID = customerConfig.getConfigUUID();
    bp.setUniverseUUID(defaultUniverse.getUniverseUUID());
    Backup backup = Backup.create(defaultCustomer.getUuid(), bp);
    backup.transitionState(BackupState.QueuedForDeletion);
    ShellResponse shellResponse = new ShellResponse();
    shellResponse.message = "{\"error\": true}";
    shellResponse.code = 2;
    when(mockStorageUtilFactory.getStorageUtil(anyString())).thenReturn(mockNfsUtil);
    when(mockTableManagerYb.deleteBackup(any())).thenReturn(shellResponse);
    backupGC.scheduleRunner();
    backup = Backup.getOrBadRequest(defaultCustomer.getUuid(), backup.getBackupUUID());
    assertEquals(BackupState.FailedToDelete, backup.getState());
  }

  @Test
  public void testDeleteCloudBackupFailure() throws Exception {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST8");
    BackupTableParams bp = new BackupTableParams();
    bp.storageConfigUUID = customerConfig.getConfigUUID();
    bp.setUniverseUUID(defaultUniverse.getUniverseUUID());
    Backup backup = Backup.create(defaultCustomer.getUuid(), bp);
    backup.transitionState(BackupState.QueuedForDeletion);
    List<String> backupLocations = new ArrayList<>();
    backupLocations.add(backup.getBackupInfo().storageLocation);
    when(mockStorageUtilFactory.getCloudUtil(anyString())).thenReturn(mockAWSUtil);
    doThrow(new RuntimeException()).when(mockAWSUtil).deleteKeyIfExists(any(), any());
    backupGC.scheduleRunner();
    backup = Backup.getOrBadRequest(defaultCustomer.getUuid(), backup.getBackupUUID());
    assertEquals(BackupState.FailedToDelete, backup.getState());
  }

  @Test
  public void testDeleteBackupWithInvalidStorageConfig() {
    BackupTableParams bp = new BackupTableParams();
    bp.storageConfigUUID = UUID.randomUUID();
    bp.setUniverseUUID(UUID.randomUUID());
    Backup backup = Backup.create(defaultCustomer.getUuid(), bp);
    backup.transitionState(BackupState.QueuedForDeletion);
    backupGC.scheduleRunner();
    backup = Backup.getOrBadRequest(defaultCustomer.getUuid(), backup.getBackupUUID());
    assertEquals(BackupState.FailedToDelete, backup.getState());
  }

  @Test
  public void testDeleteCustomerConfigSuccess() {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST9");
    BackupTableParams bp = new BackupTableParams();
    bp.storageConfigUUID = customerConfig.getConfigUUID();
    bp.setUniverseUUID(defaultUniverse.getUniverseUUID());
    // Set http context
    TestUtils.setFakeHttpContext(defaultUser);

    Backup backup = Backup.create(defaultCustomer.getUuid(), bp);
    backup.transitionState(BackupState.QueuedForDeletion);
    customerConfig.updateState(ConfigState.QueuedForDeletion);
    when(mockStorageUtilFactory.getCloudUtil(anyString())).thenReturn(mockAWSUtil);
    List<String> backupLocations = new ArrayList<>();
    backupLocations.add(backup.getBackupInfo().storageLocation);
    when(mockAWSUtil.deleteKeyIfExists(any(), anyString())).thenReturn(true);
    when(mockAWSUtil.deleteStorage(any(), any())).thenReturn(true);
    backupGC.scheduleRunner();
    assertThrows(
        PlatformServiceException.class,
        () -> Backup.getOrBadRequest(defaultCustomer.getUuid(), backup.getBackupUUID()));
    assertThrows(
        PlatformServiceException.class,
        () ->
            customerConfigService.getOrBadRequest(
                defaultCustomer.getUuid(), customerConfig.getConfigUUID()));
  }

  @Test
  public void testDeleteCustomerConfigSuccessWithBackupsLeft() {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST10");
    BackupTableParams bp = new BackupTableParams();
    bp.storageConfigUUID = customerConfig.getConfigUUID();
    bp.setUniverseUUID(defaultUniverse.getUniverseUUID());
    Backup backup = Backup.create(defaultCustomer.getUuid(), bp);
    backup.transitionState(BackupState.Completed);
    customerConfig.updateState(ConfigState.QueuedForDeletion);
    backupGC.scheduleRunner();
    assertThrows(
        PlatformServiceException.class,
        () ->
            customerConfigService.getOrBadRequest(
                defaultCustomer.getUuid(), customerConfig.getConfigUUID()));
  }

  @Test
  public void testDeleteCustomerConfigSuccessWithBackupsError() {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST11");
    BackupTableParams bp = new BackupTableParams();
    bp.storageConfigUUID = customerConfig.getConfigUUID();
    bp.setUniverseUUID(defaultUniverse.getUniverseUUID());
    Backup backup = Backup.create(defaultCustomer.getUuid(), bp);
    backup.transitionState(BackupState.QueuedForDeletion);
    doThrow(new PlatformServiceException(BAD_REQUEST, "error"))
        .when(mockBackupHelper)
        .validateStorageConfig(any());
    customerConfig.updateState(ConfigState.QueuedForDeletion);
    backupGC.scheduleRunner();
    assertThrows(
        PlatformServiceException.class,
        () ->
            customerConfigService.getOrBadRequest(
                defaultCustomer.getUuid(), customerConfig.getConfigUUID()));
    backup.refresh();
    assertEquals(BackupState.FailedToDelete, backup.getState());
  }

  @Test
  public void testDeleteExpiredBackups() {
    UUID fakeTaskUUID = buildTaskInfo(null, TaskType.DeleteBackup);
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    Backup backup =
        ModelFactory.createBackupWithExpiry(
            defaultCustomer.getUuid(),
            defaultUniverse.getUniverseUUID(),
            s3StorageConfig.getConfigUUID());
    backup.transitionState(Backup.BackupState.Completed);

    // Test that we do not delete backups of paused universe
    setUniversePaused(true, defaultUniverse);
    backupGC.scheduleRunner();
    assertEquals(
        0,
        Backup.getCompletedExpiredBackups()
            .getOrDefault(defaultCustomer.getUuid(), new ArrayList<>())
            .size());
    assertEquals(null, CustomerTask.get(defaultCustomer.getUuid(), fakeTaskUUID));
    verify(mockCommissioner, times(0)).submit(any(), any());

    // Unpause the universe and make sure that we will delete the backup.
    setUniversePaused(false, defaultUniverse);
    backupGC.scheduleRunner();
    CustomerTask task = CustomerTask.get(defaultCustomer.getUuid(), fakeTaskUUID);
    assertEquals(
        1,
        Backup.getCompletedExpiredBackups()
            .getOrDefault(defaultCustomer.getUuid(), new ArrayList<>())
            .size());
    assertEquals(CustomerTask.TaskType.Delete, task.getType());
    verify(mockCommissioner, times(1)).submit(any(), any());
  }

  @Test
  public void testDeleteExpiredChildIncrementalBackup() {
    UUID fakeTaskUUID1 = buildTaskInfo(null, TaskType.DeleteBackup);
    UUID fakeTaskUUID2 = buildTaskInfo(null, TaskType.DeleteBackup);
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID1, fakeTaskUUID2);

    Backup backup =
        ModelFactory.createBackupWithExpiry(
            defaultCustomer.getUuid(),
            defaultUniverse.getUniverseUUID(),
            s3StorageConfig.getConfigUUID());
    backup.transitionState(Backup.BackupState.Completed);

    Backup backup2 =
        ModelFactory.createBackupWithExpiry(
            defaultCustomer.getUuid(),
            defaultUniverse.getUniverseUUID(),
            s3StorageConfig.getConfigUUID());
    backup2.transitionState(Backup.BackupState.Completed);
    backup2.setBaseBackupUUID(UUID.randomUUID());
    backup2.save();

    backupGC.scheduleRunner();
    CustomerTask task = CustomerTask.get(defaultCustomer.getUuid(), fakeTaskUUID1);
    assertEquals(1, Backup.getCompletedExpiredBackups().get(defaultCustomer.getUuid()).size());
    assertEquals(CustomerTask.TaskType.Delete, task.getType());
    verify(mockCommissioner, times(1)).submit(any(), any());

    backup2.setBaseBackupUUID(backup2.getBackupUUID());
    backup2.save();
    assertEquals(2, Backup.getCompletedExpiredBackups().get(defaultCustomer.getUuid()).size());
    backupGC.scheduleRunner();
    verify(mockCommissioner, times(3)).submit(any(), any());
  }

  @Test
  public void testDeleteExpiredBackups_universeDeleted() {
    UUID fakeTaskUUID = buildTaskInfo(null, TaskType.DeleteBackup);
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);

    Backup backup =
        ModelFactory.createBackupWithExpiry(
            defaultCustomer.getUuid(),
            defaultUniverse.getUniverseUUID(),
            s3StorageConfig.getConfigUUID());
    backup.transitionState(Backup.BackupState.Completed);
    defaultUniverse.delete();
    backupGC.scheduleRunner();

    CustomerTask task = CustomerTask.get(defaultCustomer.getUuid(), fakeTaskUUID);
    assertEquals(1, Backup.getCompletedExpiredBackups().get(defaultCustomer.getUuid()).size());
    assertEquals(CustomerTask.TaskType.Delete, task.getType());
    verify(mockCommissioner, times(1)).submit(any(), any());
    assertEquals(1, Backup.getCompletedExpiredBackups().get(defaultCustomer.getUuid()).size());
  }

  @Test
  public void testDeleteExpiredBackupsCreatedFromSchedule() {
    UUID fakeTaskUUID1 = buildTaskInfo(null, TaskType.DeleteBackup);
    UUID fakeTaskUUID2 = buildTaskInfo(null, TaskType.DeleteBackup);
    UUID fakeTaskUUID3 = buildTaskInfo(null, TaskType.DeleteBackup);
    when(mockCommissioner.submit(any(), any()))
        .thenReturn(fakeTaskUUID1, fakeTaskUUID2, fakeTaskUUID3);
    Schedule s =
        ModelFactory.createScheduleBackup(
            defaultCustomer.getUuid(),
            defaultUniverse.getUniverseUUID(),
            s3StorageConfig.getConfigUUID());
    UUID fakeScheduleUUID = s.getScheduleUUID();
    for (int i = 0; i < 5; i++) {
      Backup backup =
          ModelFactory.createExpiredBackupWithScheduleUUID(
              defaultCustomer.getUuid(),
              defaultUniverse.getUniverseUUID(),
              s3StorageConfig.getConfigUUID(),
              fakeScheduleUUID);
      backup.transitionState(Backup.BackupState.Completed);
    }
    for (int i = 0; i < 2; i++) {
      Backup backup =
          ModelFactory.createBackupWithExpiry(
              defaultCustomer.getUuid(),
              defaultUniverse.getUniverseUUID(),
              s3StorageConfig.getConfigUUID());
      backup.transitionState(Backup.BackupState.Completed);
    }
    backupGC.scheduleRunner();
    assertEquals(7, Backup.getCompletedExpiredBackups().get(defaultCustomer.getUuid()).size());

    // 4 times for deleting expired backups
    verify(mockCommissioner, times(4)).submit(any(), any());
  }

  @Test
  public void testDeleteExpiredBackupsCreatedFromDeletedSchedule() {
    UUID fakeTaskUUID1 = buildTaskInfo(null, TaskType.DeleteBackup);
    UUID fakeTaskUUID2 = buildTaskInfo(null, TaskType.DeleteBackup);
    UUID fakeTaskUUID3 = buildTaskInfo(null, TaskType.DeleteBackup);
    when(mockCommissioner.submit(any(), any()))
        .thenReturn(fakeTaskUUID1, fakeTaskUUID2, fakeTaskUUID3);
    UUID fakeScheduleUUID = UUID.randomUUID();
    for (int i = 0; i < 5; i++) {
      Backup backup =
          ModelFactory.createExpiredBackupWithScheduleUUID(
              defaultCustomer.getUuid(),
              defaultUniverse.getUniverseUUID(),
              s3StorageConfig.getConfigUUID(),
              fakeScheduleUUID);
      backup.transitionState(Backup.BackupState.Completed);
    }
    for (int i = 0; i < 2; i++) {
      Backup backup =
          ModelFactory.createBackupWithExpiry(
              defaultCustomer.getUuid(),
              defaultUniverse.getUniverseUUID(),
              s3StorageConfig.getConfigUUID());
      backup.transitionState(Backup.BackupState.Completed);
    }
    backupGC.scheduleRunner();
    assertEquals(7, Backup.getCompletedExpiredBackups().get(defaultCustomer.getUuid()).size());

    // 2 time for independent and 2 times from deleted scheduled expired backups.
    verify(mockCommissioner, times(4)).submit(any(), any());
  }

  @Test
  public void testSkipAlreadyRunningDeleteBackupTask() {
    Backup backup =
        ModelFactory.createBackupWithExpiry(
            defaultCustomer.getUuid(),
            defaultUniverse.getUniverseUUID(),
            s3StorageConfig.getConfigUUID());
    backup.transitionState(Backup.BackupState.Completed);

    UUID fakeTaskUUID = buildTaskInfo(null, TaskType.DeleteBackup);
    when(mockTaskManager.isDeleteBackupTaskAlreadyPresent(
            defaultCustomer.getUuid(), backup.getBackupUUID()))
        .thenReturn(true);
    backupGC.scheduleRunner();
    assertEquals(1, Backup.getCompletedExpiredBackups().get(defaultCustomer.getUuid()).size());
    assertEquals(null, CustomerTask.get(defaultCustomer.getUuid(), fakeTaskUUID));
    verify(mockCommissioner, times(0)).submit(any(), any());

    when(mockTaskManager.isDeleteBackupTaskAlreadyPresent(
            defaultCustomer.getUuid(), backup.getBackupUUID()))
        .thenReturn(false);
    when(mockCommissioner.submit(any(), any())).thenReturn(fakeTaskUUID);
    backupGC.scheduleRunner();
    CustomerTask task = CustomerTask.get(defaultCustomer.getUuid(), fakeTaskUUID);
    assertEquals(1, Backup.getCompletedExpiredBackups().get(defaultCustomer.getUuid()).size());
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
    Universe.saveDetails(universe.getUniverseUUID(), updater);
  }
}
