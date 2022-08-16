// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.google.api.client.util.Throwables;
import com.yugabyte.yw.commissioner.YbcTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.YbcBackupUtil;
import com.yugabyte.yw.common.YbcManager;
import com.yugabyte.yw.common.services.YbcClientService;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.models.Backup;
import java.util.Date;
import java.util.concurrent.CancellationException;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections.MapUtils;
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
      for (BackupTableParams tableParams : taskParams().backupList) {
        baseLogMessage =
            ybcBackupUtil.getBaseLogMessage(tableParams.backupUuid, tableParams.getKeyspace());
        taskID =
            ybcBackupUtil.getYbcTaskID(
                tableParams.backupUuid, tableParams.backupType.name(), tableParams.getKeyspace());
        // Send create backup request to yb-controller
        try {
          ybcClient = ybcBackupUtil.getYbcClient(tableParams.universeUUID);
          BackupServiceTaskCreateRequest backupServiceTaskCreateRequest =
              ybcBackupUtil.createYbcBackupRequest(tableParams);
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
          log.error("{} Failed with error {}", baseLogMessage, e.getMessage());
          Backup backup = Backup.get(taskParams().customerUuid, taskParams().backupUuid);
          if (backup != null) {
            backup.transitionState(Backup.BackupState.Failed);
          }
          Throwables.propagate(e);
        }

        // Poll create backup progress on yb-controller and handle result
        try {
          pollTaskProgress(ybcClient, taskID);
          handleBackupResult(tableParams, idx);
        } catch (Exception e) {
          log.error("{} Failed with error {}", baseLogMessage, e.getMessage());
          Backup backup = Backup.get(taskParams().customerUuid, taskParams().backupUuid);
          if (backup != null && backup.state.equals(Backup.BackupState.InProgress)) {
            backup.transitionState(Backup.BackupState.Failed);
          }
          Throwables.propagate(e);
        }
        ybcManager.deleteYbcBackupTask(tableParams.universeUUID, taskID);
        idx++;
      }
      Backup backup = Backup.getOrBadRequest(taskParams().customerUuid, taskParams().backupUuid);
      backup.setCompletionTime(new Date(backup.getCreateTime().getTime() + totalTimeTaken));
      backup.setTotalBackupSize(totalSizeinBytes);
      backup.transitionState(Backup.BackupState.Completed);
    } catch (CancellationException ce) {
      if (ce.getMessage().contains("task aborted")) {
        ybcManager.deleteYbcBackupTask(taskParams().universeUUID, taskID);
        Throwables.propagate(ce);
      }
      ybcManager.abortBackupTask(taskParams().customerUuid, taskParams().backupUuid, taskID);
      // Backup stopped state will be updated in the main createBackup task
      Throwables.propagate(ce);
    } catch (Exception e) {
      ybcManager.deleteYbcBackupTask(taskParams().universeUUID, taskID);
      Backup backup = Backup.getOrBadRequest(taskParams().customerUuid, taskParams().backupUuid);
      backup.transitionState(Backup.BackupState.Failed);
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
