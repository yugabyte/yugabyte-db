// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.google.api.client.util.Throwables;
import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.commissioner.YbcTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.BackupUtil;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.YbcBackupUtil;
import com.yugabyte.yw.common.YbcManager;
import com.yugabyte.yw.common.YbcBackupUtil.YbcBackupResponse;
import com.yugabyte.yw.common.services.YbcClientService;
import com.yugabyte.yw.forms.BackupRequestParams;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.helpers.TaskType;
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
import play.libs.Json;

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

  public static class Params extends BackupTableParams {
    public Params(BackupTableParams tableParams) {
      super(tableParams);
    }
    // Node-ip to use as co-ordinator for the backup.
    public String nodeIp = null;
    public int index;
  }

  @Override
  public BackupTableYbc.Params taskParams() {
    return (BackupTableYbc.Params) taskParams;
  }

  @Override
  public void run() {
    try {
      ybcClient = ybcBackupUtil.getYbcClient(taskParams().universeUUID, taskParams().nodeIp);
      // Check if it is an incremental backup
      if (!taskParams().baseBackupUUID.equals(taskParams().backupUuid)) {
        previousBackup =
            Backup.getLastSuccessfulBackupInChain(
                taskParams().customerUuid, taskParams().baseBackupUUID);
        previousBackupKeyspaces =
            ybcBackupUtil.getBackupKeyspaceToParamsMap(previousBackup.getBackupInfo().backupList);
      }

      baseLogMessage =
          ybcBackupUtil.getBaseLogMessage(taskParams().backupUuid, taskParams().getKeyspace());

      TaskInfo taskInfo = TaskInfo.getOrBadRequest(userTaskUUID);
      boolean isResumable = false;
      if (taskInfo.getTaskType().equals(TaskType.CreateBackup)) {
        isResumable = true;
      }
      BackupRequestParams backupRequestParams = null;
      JsonNode backupParams = null;
      if (isResumable) {
        backupRequestParams = Json.fromJson(taskInfo.getTaskDetails(), BackupRequestParams.class);
        taskID = backupRequestParams.currentYbcTaskId;
      }

      if (taskID == null) {
        taskID =
            ybcBackupUtil.getYbcTaskID(
                taskParams().backupUuid,
                taskParams().backupType.name(),
                taskParams().getKeyspace());
        String successMarkerString = null;
        BackupTableParams previousKeyspaceParams = null;
        if (MapUtils.isNotEmpty(previousBackupKeyspaces)
            && previousBackupKeyspaces.containsKey(
                ImmutablePair.of(taskParams().backupType, taskParams().getKeyspace()))) {
          previousKeyspaceParams =
              previousBackupKeyspaces.get(
                  ImmutablePair.of(taskParams().backupType, taskParams().getKeyspace()));
          String dsmTaskId = taskID.concat(YbcBackupUtil.YBC_SUCCESS_MARKER_TASK_SUFFIX);
          successMarkerString =
              ybcManager.downloadSuccessMarker(
                  ybcBackupUtil.createDsmRequest(
                      taskParams().customerUuid,
                      taskParams().storageConfigUUID,
                      dsmTaskId,
                      previousKeyspaceParams),
                  taskParams().universeUUID,
                  dsmTaskId);
          if (StringUtils.isBlank(successMarkerString)) {
            throw new RuntimeException(
                String.format(
                    "Got empty success marker for base backup with keyspace %s",
                    taskParams().getKeyspace()));
          }
        }
        // Send create backup request to yb-controller.
        try {
          // For full backup, new keyspaces may have been introduced which were not in previous
          // backup, in such a case, previous backup won't have any context of it, even though
          // it's an incremental backup.
          BackupServiceTaskCreateRequest backupServiceTaskCreateRequest =
              previousKeyspaceParams == null
                  ? ybcBackupUtil.createYbcBackupRequest(taskParams())
                  : ybcBackupUtil.createYbcBackupRequest(taskParams(), previousKeyspaceParams);
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
          } else if (response.getStatus().getCode().equals(ControllerStatus.EXISTS)) {
            log.info(
                String.format(
                    "%s Already present on YB-controller with taskID: %s", baseLogMessage, taskID));
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
      }

      if (isResumable) {
        backupRequestParams.currentYbcTaskId = taskID;
        backupRequestParams.currentIdx = taskParams().index;
        backupParams = Json.toJson(backupRequestParams);
        getRunnableTask().setTaskDetails(backupParams);
      }

      // Poll create backup progress on yb-controller and handle result
      try {
        pollTaskProgress(ybcClient, taskID);
        handleBackupResult();
        if (isResumable) {
          backupRequestParams.currentYbcTaskId = null;
          backupRequestParams.currentIdx++;
          backupParams = Json.toJson(backupRequestParams);
          getRunnableTask().setTaskDetails(backupParams);
        }
      } catch (Exception e) {
        log.error(
            "{} Polling backup task progress on YB-Controller failed with error {}",
            baseLogMessage,
            e.getMessage());
        Throwables.propagate(e);
      }
      ybcManager.deleteYbcBackupTask(taskParams().universeUUID, taskID, ybcClient);
    } catch (CancellationException ce) {
      Throwables.propagate(ce);
    } catch (Throwable e) {
      // Backup state will be set to Failed in main task.
      if (StringUtils.isNotBlank(taskID)) {
        ybcManager.deleteYbcBackupTask(taskParams().universeUUID, taskID, ybcClient);
      }
      Throwables.propagate(e);
    } finally {
      if (ybcClient != null) {
        ybcService.closeClient(ybcClient);
      }
    }
  }

  /** Update backup object with success metadata */
  private void handleBackupResult() throws PlatformServiceException {
    BackupServiceTaskResultRequest backupServiceTaskResultRequest =
        ybcBackupUtil.createYbcBackupResultRequest(taskID);
    BackupServiceTaskResultResponse backupServiceTaskResultResponse =
        ybcClient.backupServiceTaskResult(backupServiceTaskResultRequest);
    if (backupServiceTaskResultResponse.getTaskStatus().equals(ControllerStatus.OK)) {
      Backup backup = Backup.getOrBadRequest(taskParams().customerUuid, taskParams().backupUuid);
      YbcBackupUtil.YbcBackupResponse response =
          ybcBackupUtil.parseYbcBackupResponse(backupServiceTaskResultResponse.getMetadataJson());
      long backupSize = Long.parseLong(response.backupSize);
      backup.onPartialCompletion(
          taskParams().index, backupServiceTaskResultResponse.getTimeTakenMs(), backupSize);

      // Add specific storage locations for regional backups
      if (MapUtils.isNotEmpty(response.responseCloudStoreSpec.regionLocations)) {
        backup.setPerRegionLocations(
            taskParams().index,
            ybcBackupUtil.extractRegionLocationFromMetadata(
                response.responseCloudStoreSpec.regionLocations, taskParams()));
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
