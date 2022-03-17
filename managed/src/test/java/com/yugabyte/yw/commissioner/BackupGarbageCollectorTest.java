// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.when;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.doNothing;
import static org.mockito.Mockito.doThrow;
import static play.mvc.Http.Status.BAD_REQUEST;

import akka.actor.ActorSystem;
import akka.actor.Scheduler;
import com.yugabyte.yw.common.AWSUtil;
import com.yugabyte.yw.common.GCPUtil;
import com.yugabyte.yw.common.AZUtil;
import com.yugabyte.yw.common.BackupUtil;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.TableManagerYb;
import com.yugabyte.yw.common.customer.config.CustomerConfigService;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerConfig;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Backup.BackupState;
import com.yugabyte.yw.models.CustomerConfig.ConfigState;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import java.util.UUID;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.MockedStatic;
import org.mockito.Mockito;
import org.mockito.junit.MockitoJUnitRunner;
import scala.concurrent.ExecutionContext;

@RunWith(MockitoJUnitRunner.class)
public class BackupGarbageCollectorTest extends FakeDBApplication {

  @Mock ActorSystem mockActorSystem;

  @Mock ExecutionContext mockExecutionContext;

  @Mock Scheduler mockScheduler;

  @Mock RuntimeConfigFactory mockRuntimeConfigFactory;

  MockedStatic<AWSUtil> mockAWSUtil;

  MockedStatic<GCPUtil> mockGCPUtil;

  MockedStatic<AZUtil> mockAZUtil;

  private Customer defaultCustomer;
  private Universe defaultUniverse;
  private BackupGarbageCollector backupGC;
  private CustomerConfigService customerConfigService;
  private TableManagerYb tableManagerYb;
  private BackupUtil mockBackupUtil;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    defaultUniverse = ModelFactory.createUniverse(defaultCustomer.getCustomerId());
    customerConfigService = app.injector().instanceOf(CustomerConfigService.class);
    tableManagerYb = app.injector().instanceOf(TableManagerYb.class);
    mockAWSUtil = Mockito.mockStatic(AWSUtil.class);
    mockGCPUtil = Mockito.mockStatic(GCPUtil.class);
    mockAZUtil = Mockito.mockStatic(AZUtil.class);
    mockBackupUtil = mock(BackupUtil.class);
    backupGC =
        new BackupGarbageCollector(
            mockExecutionContext,
            mockActorSystem,
            customerConfigService,
            mockRuntimeConfigFactory,
            tableManagerYb,
            mockBackupUtil);
  }

  @After
  public void tearDown() {
    mockAWSUtil.close();
    mockGCPUtil.close();
    mockAZUtil.close();
  }

  @Test
  public void testDeleteAWSBackupSuccess() {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST1");
    BackupTableParams bp = new BackupTableParams();
    bp.storageConfigUUID = customerConfig.configUUID;
    bp.universeUUID = UUID.randomUUID();
    Backup backup = Backup.create(defaultCustomer.uuid, bp);
    backup.transitionState(BackupState.QueuedForDeletion);
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
  public void testDeleteCloudBackupFailure() {
    CustomerConfig customerConfig = ModelFactory.createS3StorageConfig(defaultCustomer, "TEST8");
    BackupTableParams bp = new BackupTableParams();
    bp.storageConfigUUID = customerConfig.configUUID;
    bp.universeUUID = defaultUniverse.universeUUID;
    Backup backup = Backup.create(defaultCustomer.uuid, bp);
    backup.transitionState(BackupState.QueuedForDeletion);
    mockAWSUtil
        .when(() -> AWSUtil.deleteKeyIfExists(any(), any()))
        .thenThrow(new RuntimeException());
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
}
