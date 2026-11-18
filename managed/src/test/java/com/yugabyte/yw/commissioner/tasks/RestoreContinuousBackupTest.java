// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static org.junit.Assert.assertThrows;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.ArgumentMatchers.same;
import static org.mockito.Mockito.doThrow;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.TaskExecutor;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.StorageUtil;
import com.yugabyte.yw.common.StorageUtilFactory;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.backuprestore.BackupHelper;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.ha.PlatformReplicationHelper;
import com.yugabyte.yw.common.ha.PlatformReplicationManager;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.configs.data.CustomerConfigData;
import java.io.File;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Set;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.mockito.ArgumentCaptor;

/**
 * Restoring a continuous YBA backup reaches the bucket with the identity BackupHelper sets on one
 * in-memory copy of the storage config, so every bucket call must get that copy, and a bucket YBA
 * cannot reach must fail the restore before anything is downloaded.
 */
public class RestoreContinuousBackupTest extends FakeDBApplication {

  private static final String BACKUP_DIR = "test";
  private static final String RELEASE_VERSION = "2.33.0.0-b1";

  private BackupHelper mockBackupHelper;
  private StorageUtil mockStorageUtil;
  private PlatformReplicationManager mockReplicationManager;
  private RestoreContinuousBackup task;

  @Before
  public void setUp() {
    Customer customer = ModelFactory.testCustomer();
    CustomerConfig storageConfig = ModelFactory.createS3StorageConfig(customer, "fed-s3");
    mockBackupHelper = mock(BackupHelper.class);
    mockStorageUtil = mock(StorageUtil.class);
    mockReplicationManager = mock(PlatformReplicationManager.class);
    StorageUtilFactory mockStorageUtilFactory = mock(StorageUtilFactory.class);
    when(mockStorageUtilFactory.getStorageUtil(any())).thenReturn(mockStorageUtil);
    // The task reads its own TaskInfo from the executor before touching the bucket.
    TaskInfo taskInfo = mock(TaskInfo.class);
    when(taskInfo.getUuid()).thenReturn(UUID.randomUUID());
    TaskExecutor.RunnableTask runnableTask = mock(TaskExecutor.RunnableTask.class);
    when(runnableTask.getTaskInfo()).thenReturn(taskInfo);
    TaskExecutor mockTaskExecutor = mock(TaskExecutor.class);
    when(mockTaskExecutor.getRunnableTask(any())).thenReturn(runnableTask);
    BaseTaskDependencies deps = mock(BaseTaskDependencies.class);
    when(deps.getBackupHelper()).thenReturn(mockBackupHelper);
    when(deps.getReleaseManager()).thenReturn(mockReleaseManager);
    when(deps.getTaskExecutor()).thenReturn(mockTaskExecutor);
    Config mockAppConfig = mock(Config.class);
    when(mockAppConfig.getString(Util.YB_RELEASES_PATH)).thenReturn("/tmp/releases");
    task =
        new RestoreContinuousBackup(
            deps,
            mock(PlatformReplicationHelper.class),
            mockReplicationManager,
            mockStorageUtilFactory,
            mockAppConfig,
            mock(RuntimeConfGetter.class));
    RestoreContinuousBackup.Params params = new RestoreContinuousBackup.Params();
    params.storageConfigUUID = storageConfig.getConfigUUID();
    params.backupDir = BACKUP_DIR;
    task.initialize(params);
  }

  /** A bucket YBA has no way to reach fails the restore before anything is downloaded. */
  @Test
  public void testUnreachableBucketFailsBeforeTheDownload() {
    doThrow(new PlatformServiceException(BAD_REQUEST, "no credentials"))
        .when(mockBackupHelper)
        .requireYbaFederatedIdentity(any(), any());

    assertThrows(PlatformServiceException.class, () -> task.run());

    verify(mockStorageUtil, never()).downloadYbaBackup(any(), any(), any());
  }

  /** Every bucket call gets the copy of the config that the identity was set on. */
  @Test
  public void testEveryBucketCallGetsTheConfigTheIdentityWasSetOn() throws Exception {
    File backup = File.createTempFile("yba-backup", ".tgz");
    backup.deleteOnExit();
    when(mockStorageUtil.downloadYbaBackup(any(), any(), any())).thenReturn(backup);
    when(mockStorageUtil.getRemoteReleaseVersions(any(), any()))
        .thenReturn(new HashSet<>(Set.of(RELEASE_VERSION)));
    when(mockReleaseManager.getAllLocalReleaseContainersByVersion()).thenReturn(new HashMap<>());
    when(mockStorageUtil.downloadRemoteReleases(any(), any(), any(), any())).thenReturn(true);
    // Stop before the restore, which would restart YBA.
    when(mockReplicationManager.restoreBackup(any(), anyBoolean())).thenReturn(false);

    assertThrows(PlatformServiceException.class, () -> task.run());

    ArgumentCaptor<CustomerConfigData> stamped = ArgumentCaptor.forClass(CustomerConfigData.class);
    verify(mockBackupHelper).requireYbaFederatedIdentity(any(), stamped.capture());
    verify(mockStorageUtil).downloadYbaBackup(same(stamped.getValue()), eq(BACKUP_DIR), any());
    verify(mockStorageUtil).getRemoteReleaseVersions(same(stamped.getValue()), eq(BACKUP_DIR));
    verify(mockStorageUtil)
        .downloadRemoteReleases(
            same(stamped.getValue()),
            eq(Set.of(RELEASE_VERSION)),
            eq("/tmp/releases"),
            eq(BACKUP_DIR));
  }
}
