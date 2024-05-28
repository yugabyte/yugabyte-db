// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.ArgumentMatchers.isNull;
import static org.mockito.ArgumentMatchers.nullable;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.forms.BackupRequestParams;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Backup.BackupCategory;
import com.yugabyte.yw.models.Backup.BackupState;
import com.yugabyte.yw.models.Backup.BackupVersion;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.UUID;
import java.util.stream.Collectors;
import org.apache.commons.collections4.CollectionUtils;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;
import org.yb.client.ListSnapshotSchedulesResponse;
import org.yb.client.ListSnapshotsResponse;
import org.yb.client.SnapshotInfo;
import org.yb.client.SnapshotScheduleInfo;
import org.yb.client.YBClient;
import org.yb.master.CatalogEntityInfo.SysSnapshotEntryPB.State;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class SnapshotCleanupTest extends FakeDBApplication {
  @Mock RuntimeConfGetter mockConfGetter;

  private SnapshotCleanup mockSnapshotCleanup;
  private Customer testCustomer;
  private Universe testUniverse;
  private YBClient mockYBClient = null;

  @Before
  public void setup() {
    mockSnapshotCleanup = new SnapshotCleanup(mockService, mockConfGetter);
    testCustomer = ModelFactory.testCustomer();
    testUniverse = ModelFactory.createUniverse(testCustomer.getId());
    when(mockConfGetter.getConfForScope(
            testUniverse, UniverseConfKeys.deleteOrphanSnapshotOnStartup))
        .thenReturn(true);
    mockYBClient = mock(YBClient.class);
    when(mockService.getClient(nullable(String.class), nullable(String.class)))
        .thenReturn(mockYBClient);
  }

  @Test
  public void testGetNonScheduledSnapshots() throws Exception {
    UUID testScheduledSnapshotInfo_1_UUID = UUID.randomUUID();
    UUID testScheduledSnapshotInfo_2_UUID = UUID.randomUUID();
    SnapshotInfo testScheduledSnapshotInfo_1 =
        new SnapshotInfo(testScheduledSnapshotInfo_1_UUID, 3600, 0, State.COMPLETE);
    SnapshotInfo testScheduledSnapshotInfo_2 =
        new SnapshotInfo(testScheduledSnapshotInfo_2_UUID, 7200, 3600, State.COMPLETE);
    List<SnapshotInfo> testScheduledSnapshotInfoList = new ArrayList<>();
    testScheduledSnapshotInfoList.add(testScheduledSnapshotInfo_1);
    testScheduledSnapshotInfoList.add(testScheduledSnapshotInfo_2);

    UUID testSchedule_UUID = UUID.randomUUID();
    SnapshotScheduleInfo testScheduleInfo =
        new SnapshotScheduleInfo(testSchedule_UUID, 3600, 3600, testScheduledSnapshotInfoList);
    ListSnapshotSchedulesResponse mockSchedulesResponse = mock(ListSnapshotSchedulesResponse.class);
    when(mockYBClient.listSnapshotSchedules(isNull())).thenReturn(mockSchedulesResponse);
    when(mockSchedulesResponse.getSnapshotScheduleInfoList())
        .thenReturn(Arrays.asList(testScheduleInfo));

    UUID testNonScheduledSnapshotInfo_1_UUID = UUID.randomUUID();
    SnapshotInfo testNonScheduledSnapshotInfo =
        new SnapshotInfo(testNonScheduledSnapshotInfo_1_UUID, 3600, 0, State.COMPLETE);

    List<SnapshotInfo> allSnapshotInfos = new ArrayList<>();
    allSnapshotInfos.addAll(testScheduledSnapshotInfoList);
    allSnapshotInfos.add(testNonScheduledSnapshotInfo);
    ListSnapshotsResponse mockListResponse = mock(ListSnapshotsResponse.class);
    when(mockListResponse.getSnapshotInfoList()).thenReturn(allSnapshotInfos);
    when(mockYBClient.listSnapshots(isNull(), eq(false))).thenReturn(mockListResponse);

    List<SnapshotInfo> filterResponseList =
        mockSnapshotCleanup.getNonScheduledSnapshotList(testUniverse);
    assertTrue(filterResponseList.size() == 1);
    assertEquals(filterResponseList.get(0).getSnapshotUUID(), testNonScheduledSnapshotInfo_1_UUID);
  }

  @Test
  public void testFilterRestoreSnapshots() throws Exception {
    UUID testNonScheduledSnapshotInfo_1_UUID = UUID.randomUUID();
    SnapshotInfo testNonScheduledSnapshotInfo =
        new SnapshotInfo(testNonScheduledSnapshotInfo_1_UUID, 3600, 0, State.COMPLETE);
    UUID testRestoreTypeSnapshot_UUID = UUID.randomUUID();
    SnapshotInfo testRestoreTypeSnapshotInfo =
        new SnapshotInfo(testRestoreTypeSnapshot_UUID, 3600, 0, State.RESTORING);

    List<SnapshotInfo> allSnapshotInfos = new ArrayList<>();
    allSnapshotInfos.add(testNonScheduledSnapshotInfo);
    allSnapshotInfos.add(testRestoreTypeSnapshotInfo);

    ListSnapshotsResponse mockListResponse = mock(ListSnapshotsResponse.class);
    when(mockListResponse.getSnapshotInfoList()).thenReturn(allSnapshotInfos);
    when(mockYBClient.listSnapshots(isNull(), eq(false))).thenReturn(mockListResponse);

    List<SnapshotInfo> filterResponseList =
        mockSnapshotCleanup.getNonScheduledSnapshotList(testUniverse);
    assertTrue(filterResponseList.size() == 1);
    assertEquals(filterResponseList.get(0).getSnapshotUUID(), testNonScheduledSnapshotInfo_1_UUID);
  }

  @Test
  public void testInProgressBackupFilter() throws Exception {
    Backup backup = new Backup();
    backup.setBackupUUID(UUID.randomUUID());
    backup.setCustomerUUID(testCustomer.getUuid());
    backup.setUniverseUUID(testUniverse.getUniverseUUID());
    backup.setState(BackupState.InProgress);
    backup.setStorageConfigUUID(UUID.randomUUID());
    backup.setCategory(BackupCategory.YB_CONTROLLER);
    backup.setVersion(BackupVersion.V2);
    backup.setBaseBackupUUID(backup.getBackupUUID());
    backup.setBackupInfo(new BackupTableParams());
    backup.save();
    TaskInfo taskInfo = new TaskInfo(TaskType.CreateBackup, null);
    BackupRequestParams bParams = new BackupRequestParams();
    bParams.backupUUID = backup.getBackupUUID();
    bParams.setUniverseUUID(testUniverse.getUniverseUUID());
    taskInfo.setTaskParams(Json.toJson(bParams));
    taskInfo.setOwner("unknown");
    taskInfo.save();

    SnapshotInfo testInProgressBackupSnapshot =
        new SnapshotInfo(
            UUID.randomUUID(), backup.getCreateTime().getTime() + 1, 0, State.COMPLETE);
    SnapshotInfo testOrphanSnapshot = new SnapshotInfo(UUID.randomUUID(), 3600, 0, State.COMPLETE);
    List<SnapshotInfo> allSnapshotInfos = new ArrayList<>();
    allSnapshotInfos.add(testInProgressBackupSnapshot);
    allSnapshotInfos.add(testOrphanSnapshot);

    ListSnapshotsResponse mockListResponse = mock(ListSnapshotsResponse.class);
    when(mockListResponse.getSnapshotInfoList()).thenReturn(allSnapshotInfos);
    when(mockYBClient.listSnapshots(isNull(), eq(false))).thenReturn(mockListResponse);
    when(mockYBClient.listSnapshotSchedules(isNull())).thenReturn(null);
    when(mockYBClient.deleteSnapshot(any())).thenReturn(null);
    mockSnapshotCleanup.deleteOrphanSnapshots();
    ArgumentCaptor<UUID> uuidCaptor = ArgumentCaptor.forClass(UUID.class);
    verify(mockYBClient, times(1)).deleteSnapshot(uuidCaptor.capture());
    assertTrue(uuidCaptor.getAllValues().size() == 1);
    assertEquals(uuidCaptor.getAllValues().get(0), testOrphanSnapshot.getSnapshotUUID());
  }

  @Test
  public void testNoBackupInProgress() throws Exception {
    SnapshotInfo testOrphanSnapshot_1 =
        new SnapshotInfo(UUID.randomUUID(), 3600, 0, State.COMPLETE);
    SnapshotInfo testOrphanSnapshot_2 =
        new SnapshotInfo(UUID.randomUUID(), 3600, 0, State.COMPLETE);
    List<SnapshotInfo> allSnapshotInfos = new ArrayList<>();
    allSnapshotInfos.add(testOrphanSnapshot_1);
    allSnapshotInfos.add(testOrphanSnapshot_2);

    ListSnapshotsResponse mockListResponse = mock(ListSnapshotsResponse.class);
    when(mockListResponse.getSnapshotInfoList()).thenReturn(allSnapshotInfos);
    when(mockYBClient.listSnapshots(isNull(), eq(false))).thenReturn(mockListResponse);
    when(mockYBClient.listSnapshotSchedules(isNull())).thenReturn(null);
    when(mockYBClient.deleteSnapshot(any())).thenReturn(null);
    mockSnapshotCleanup.deleteOrphanSnapshots();
    ArgumentCaptor<UUID> uuidCaptor = ArgumentCaptor.forClass(UUID.class);
    verify(mockYBClient, times(2)).deleteSnapshot(uuidCaptor.capture());
    assertTrue(uuidCaptor.getAllValues().size() == 2);
    assertTrue(
        CollectionUtils.isEqualCollection(
            uuidCaptor.getAllValues(),
            allSnapshotInfos.stream()
                .map(sI -> sI.getSnapshotUUID())
                .collect(Collectors.toList())));
  }

  @Test
  public void testRuntimeConfDisableSnapshotCleanup() throws Exception {
    when(mockConfGetter.getConfForScope(
            testUniverse, UniverseConfKeys.deleteOrphanSnapshotOnStartup))
        .thenReturn(false);
    mockSnapshotCleanup.deleteOrphanSnapshots();
    verify(mockYBClient, times(0)).listSnapshotSchedules(isNull());
    verify(mockYBClient, times(0)).listSnapshots(isNull(), eq(false));
  }
}
