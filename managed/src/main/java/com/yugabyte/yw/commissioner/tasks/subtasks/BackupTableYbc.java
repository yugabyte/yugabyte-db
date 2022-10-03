// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.google.api.client.util.Throwables;
import com.yugabyte.yw.commissioner.YbcTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.BackupUtil;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.YbcBackupUtil;
import com.yugabyte.yw.common.YbcManager;
import com.yugabyte.yw.common.YbcBackupUtil.YbcBackupResponse;
import com.yugabyte.yw.common.services.YbcClientService;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.models.Backup;
import java.util.Date;
import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.CancellationException;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections.MapUtils;
import org.apache.commons.lang.StringUtils;
import org.apache.commons.lang3.tuple.ImmutablePair;
import org.yb.CommonTypes.TableType;
import org.yb.client.YbcClient;
import org.yb.ybc.BackupServiceTaskCreateRequest;
import org.yb.ybc.BackupServiceTaskCreateResponse;
import org.yb.ybc.BackupServiceTaskResultRequest;
import org.yb.ybc.BackupServiceTaskResultResponse;
import org.yb.ybc.ControllerStatus;

@Slf4j
public class BackupTableYbc extends YbcTaskBase {

  private final YbcManager ybcManager;
  private YbcClient ybcClient;
  private long totalTimeTaken = 0L;
  private long totalSizeinBytes = 0L;
  private String baseLogMessage = null;
  private String taskID = null;
  private Backup previousBackup = null;
  private Map<ImmutablePair<TableType, String>, BackupTableParams> previousBackupKeyspaces =
      new HashMap<>();

  @Inject
  public BackupTableYbc(
      BaseTaskDependencies baseTaskDependencies,
      YbcClientService ybcService,
      YbcBackupUtil ybcBackupUtil,
      YbcManager ybcManager) {
    super(baseTaskDependencies, ybcService, ybcBackupUtil);
    this.ybcManager = ybcManager;
  }

  @Override
  public BackupTableParams taskParams() {
    return (BackupTableParams) taskParams;
  }

  @Override
  public void run() {
    int idx = 0;
    try {
      ybcClient = ybcBackupUtil.getYbcClient(taskParams().universeUUID);
      // Check if it is an incremental backup
      if (taskParams().baseBackupUUID != taskParams().backupUuid) {
        previousBackup =
            Backup.getLastSuccessfulBackupInChain(
                taskParams().customerUuid, taskParams().baseBackupUUID);
        previousBackupKeyspaces =
            ybcBackupUtil.getBackupKeyspaceToParamsMap(previousBackup.getBackupInfo().backupList);
      }
      for (BackupTableParams tableParams : taskParams().backupList) {
        baseLogMessage =
            ybcBackupUtil.getBaseLogMessage(tableParams.backupUuid, tableParams.getKeyspace());
        taskID =
            ybcBackupUtil.getYbcTaskID(
                tableParams.backupUuid, tableParams.backupType.name(), tableParams.getKeyspace());
        String successMarkerString = null;
        BackupTableParams previousKeyspaceParams = null;
        if (MapUtils.isNotEmpty(previousBackupKeyspaces)
            && previousBackupKeyspaces.containsKey(
                ImmutablePair.of(tableParams.backupType, tableParams.getKeyspace()))) {
          previousKeyspaceParams =
              previousBackupKeyspaces.get(
                  ImmutablePair.of(tableParams.backupType, tableParams.getKeyspace()));
          successMarkerString =
              ybcManager.downloadSuccessMarker(
                  ybcBackupUtil.createDsmRequest(
                      taskParams().customerUuid,
                      taskParams().storageConfigUUID,
                      taskID,
                      previousKeyspaceParams),
                  taskParams().universeUUID,
                  taskID);
          if (StringUtils.isBlank(successMarkerString)) {
            throw new RuntimeException(
                String.format(
                    "Got empty success marker for base backup with keyspace %s",
                    tableParams.getKeyspace()));
          }
        }
        // Send create backup request to yb-controller.
        try {
          // For full backup, new keyspaces may have been introduced which were not in previous
          // backup, in such a case, previous backup won't have any context of it, even though
          // it's an incremental backup.
          BackupServiceTaskCreateRequest backupServiceTaskCreateRequest =
              previousKeyspaceParams == null
                  ? ybcBackupUtil.createYbcBackupRequest(tableParams)
                  : ybcBackupUtil.createYbcBackupRequest(tableParams, previousKeyspaceParams);
          if (previousKeyspaceParams != null) {
            // Fail if validation fails.
            YbcBackupResponse successMarker =
                ybcBackupUtil.parseYbcBackupResponse(successMarkerString);
            ybcBackupUtil.validateConfigWithSuccessMarker(
                successMarker, backupServiceTaskCreateRequest.getCsConfig(), true);
          }
          BackupServiceTaskCreateResponse response =
              ybcClient.backupNamespace(backupServiceTaskCreateRequest);
          if (response.getStatus().getCode().equals(ControllerStatus.OK)) {
            log.info(
                String.format(
                    "%s Successfully submitted backup task to YB-controller with taskID: %s",
                    baseLogMessage, taskID));
          } else {
            throw new PlatformServiceException(
                response.getStatus().getCodeValue(),
                String.format(
                    "%s YB-controller returned non-zero exit status %s",
                    baseLogMessage, response.getStatus().getErrorMessage()));
          }
        } catch (Exception e) {
          log.error(
              "{} Sending backup request to YB-Controller failed with error {}",
              baseLogMessage,
              e.getMessage());
          Throwables.propagate(e);
        }

        // Poll create backup progress on yb-controller and handle result
        try {
          pollTaskProgress(ybcClient, taskID);
          handleBackupResult(tableParams, idx);
        } catch (Exception e) {
          log.error(
              "{} Polling backup task progress on YB-Controller failed with error {}",
              baseLogMessage,
              e.getMessage());
          Throwables.propagate(e);
        }
        ybcManager.deleteYbcBackupTask(tableParams.universeUUID, taskID);
        idx++;
      }
      Backup backup = Backup.getOrBadRequest(taskParams().customerUuid, taskParams().backupUuid);

      // Update base backup's expiry time if this increment succeeds and expires after the base
      // backup.
      if (taskParams().baseBackupUUID != taskParams().backupUuid) {
        Backup baseBackup =
            Backup.getOrBadRequest(taskParams().customerUuid, taskParams().baseBackupUUID);
        baseBackup.onIncrementCompletion(backup.getExpiry(), totalSizeinBytes);
        // Unset expiry time for increment, only the base backup's expiry is what we need.
        backup.onCompletion(totalTimeTaken, totalSizeinBytes, true);
      } else {
        backup.onCompletion(totalTimeTaken, totalSizeinBytes, false);
      }
    } catch (CancellationException ce) {
      if (!ce.getMessage().contains("Yb-Controller task aborted")) {
        ybcManager.abortBackupTask(taskParams().customerUuid, taskParams().backupUuid, taskID);
      }
      ybcManager.deleteYbcBackupTask(taskParams().universeUUID, taskID);
      // Backup stopped state will be updated in the main createBackup task.
      Throwables.propagate(ce);
    } catch (Throwable e) {
      // Backup state will be set to Failed in main task.
      ybcManager.deleteYbcBackupTask(taskParams().universeUUID, taskID);
      Throwables.propagate(e);
    } finally {
      if (ybcClient != null) {
        ybcService.closeClient(ybcClient);
      }
    }
  }

  /**
   * Update backup object with success metadata
   *
   * @param tableParams
   * @param idx
   */
  private void handleBackupResult(BackupTableParams tableParams, int idx)
      throws PlatformServiceException {
    BackupServiceTaskResultRequest backupServiceTaskResultRequest =
        ybcBackupUtil.createYbcBackupResultRequest(taskID);
    BackupServiceTaskResultResponse backupServiceTaskResultResponse =
        ybcClient.backupServiceTaskResult(backupServiceTaskResultRequest);
    if (backupServiceTaskResultResponse.getTaskStatus().equals(ControllerStatus.OK)) {
      Backup backup = Backup.getOrBadRequest(taskParams().customerUuid, taskParams().backupUuid);
      YbcBackupUtil.YbcBackupResponse response =
          ybcBackupUtil.parseYbcBackupResponse(backupServiceTaskResultResponse.getMetadataJson());
      long backupSize = Long.parseLong(response.backupSize);
      backup.setBackupSizeInBackupList(idx, backupSize);
      totalSizeinBytes += backupSize;
      totalTimeTaken += backupServiceTaskResultResponse.getTimeTakenMs();

      // Add specific storage locations for regional backups
      if (MapUtils.isNotEmpty(response.responseCloudStoreSpec.regionLocations)) {
        backup.setPerRegionLocations(
            idx,
            ybcBackupUtil.extractRegionLocationFromMetadata(
                response.responseCloudStoreSpec.regionLocations, tableParams));
      }
    } else {
      throw new PlatformServiceException(
          backupServiceTaskResultResponse.getTaskStatus().getNumber(),
          String.format(
              "%s YB-controller returned non-zero exit status %s",
              baseLogMessage, backupServiceTaskResultResponse.getTaskStatus().name()));
    }
  }
}
