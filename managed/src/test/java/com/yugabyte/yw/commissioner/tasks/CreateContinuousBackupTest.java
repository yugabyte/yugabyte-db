// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static org.junit.Assert.assertThrows;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.ArgumentMatchers.same;
import static org.mockito.Mockito.doThrow;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.StorageUtil;
import com.yugabyte.yw.common.StorageUtilFactory;
import com.yugabyte.yw.common.backuprestore.BackupHelper;
import com.yugabyte.yw.common.ha.PlatformReplicationHelper;
import com.yugabyte.yw.common.ha.PlatformReplicationManager;
import com.yugabyte.yw.common.ha.PlatformReplicationManager.CreatePlatformBackupParams;
import com.yugabyte.yw.models.ContinuousBackupConfig;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.configs.data.CustomerConfigData;
import com.yugabyte.yw.models.helpers.TimeUnit;
import java.io.File;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Optional;
import org.junit.Before;
import org.junit.Test;
import org.mockito.ArgumentCaptor;

/**
 * The continuous YBA backup reaches a federated bucket with the identity BackupHelper sets on the
 * storage config. That identity is set on one in-memory copy of the config, so every bucket call
 * must get that copy, and a bucket YBA cannot reach must fail the run before a backup is made.
 */
public class CreateContinuousBackupTest extends FakeDBApplication {

  private static final String BACKUP_DIR = "test";

  private BackupHelper mockBackupHelper;
  private PlatformReplicationHelper mockReplicationHelper;
  private StorageUtil mockStorageUtil;
  private CreateContinuousBackup task;

  @Before
  public void setUp() {
    Customer customer = ModelFactory.testCustomer();
    CustomerConfig storageConfig = ModelFactory.createS3StorageConfig(customer, "fed-s3");
    mockBackupHelper = mock(BackupHelper.class);
    mockReplicationHelper = mock(PlatformReplicationHelper.class);
    mockStorageUtil = mock(StorageUtil.class);
    StorageUtilFactory mockStorageUtilFactory = mock(StorageUtilFactory.class);
    when(mockStorageUtilFactory.getStorageUtil(any())).thenReturn(mockStorageUtil);
    BaseTaskDependencies deps = mock(BaseTaskDependencies.class);
    when(deps.getBackupHelper()).thenReturn(mockBackupHelper);
    when(deps.getReleaseManager()).thenReturn(mockReleaseManager);
    task =
        new CreateContinuousBackup(
            deps,
            mockReplicationHelper,
            app.injector().instanceOf(PlatformReplicationManager.class),
            mockStorageUtilFactory);
    ContinuousBackupConfig cbConfig =
        ContinuousBackupConfig.create(
            storageConfig.getConfigUUID(), 5, TimeUnit.MINUTES, 3, BACKUP_DIR);
    cbConfig.updateStorageLocation("s3://bucket/" + BACKUP_DIR);
    CreateContinuousBackup.Params params = new CreateContinuousBackup.Params();
    params.cbConfig = cbConfig;
    task.initialize(params);
  }

  /** A bucket YBA has no way to reach fails the run without making a platform backup. */
  @Test
  public void testUnreachableBucketFailsBeforeTheBackupIsMade() {
    doThrow(new PlatformServiceException(BAD_REQUEST, "no credentials"))
        .when(mockBackupHelper)
        .requireYbaFederatedIdentity(any(), any());

    assertThrows(PlatformServiceException.class, () -> task.run());

    verify(mockReplicationHelper, never()).runCommand(any(CreatePlatformBackupParams.class));
    verify(mockStorageUtil, never()).uploadYbaBackup(any(), any(), any());
  }

  /** Every bucket call gets the copy of the config that the identity was set on. */
  @Test
  public void testEveryBucketCallGetsTheConfigTheIdentityWasSetOn() throws Exception {
    File backup = File.createTempFile("yba-backup", ".tgz");
    backup.deleteOnExit();
    when(mockReplicationHelper.runCommand(any(CreatePlatformBackupParams.class)))
        .thenReturn(ShellResponse.create(0, "ok"));
    when(mockReplicationHelper.getMostRecentBackup()).thenReturn(Optional.of(backup));
    when(mockStorageUtil.uploadYbaBackup(any(), any(), any())).thenReturn(true);
    when(mockStorageUtil.cleanupUploadedBackups(any(), any())).thenReturn(true);
    when(mockStorageUtil.getRemoteReleaseVersions(any(), any())).thenReturn(new HashSet<>());
    when(mockReleaseManager.getAllLocalReleaseContainersByVersion()).thenReturn(new HashMap<>());

    task.run();

    ArgumentCaptor<CustomerConfigData> stamped = ArgumentCaptor.forClass(CustomerConfigData.class);
    verify(mockBackupHelper).requireYbaFederatedIdentity(any(), stamped.capture());
    verify(mockStorageUtil).uploadYbaBackup(same(stamped.getValue()), eq(backup), eq(BACKUP_DIR));
    verify(mockStorageUtil).cleanupUploadedBackups(same(stamped.getValue()), eq(BACKUP_DIR));
    verify(mockStorageUtil).getRemoteReleaseVersions(same(stamped.getValue()), eq(BACKUP_DIR));
  }
}
