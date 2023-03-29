// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.google.api.client.util.Throwables;
import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.commissioner.YbcTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.TaskExecutor;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.services.YbcClientService;
import com.yugabyte.yw.common.ybc.YbcBackupNodeRetriever;
import com.yugabyte.yw.common.ybc.YbcBackupUtil;
import com.yugabyte.yw.common.ybc.YbcManager;
import com.yugabyte.yw.common.ybc.YbcBackupUtil.YbcBackupResponse;
import com.yugabyte.yw.forms.BackupRequestParams;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.forms.BackupRequestParams.ParallelBackupState;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import java.time.Duration;
import java.util.Date;
import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.CancellationException;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections.MapUtils;
import org.apache.commons.lang3.StringUtils;
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
  private TaskExecutor taskExecutor;
  private String baseLogMessage = null;
  private Backup previousBackup = null;
  private Map<ImmutablePair<TableType, String>, BackupTableParams> previousBackupKeyspaces =
      new HashMap<>();

  @Inject
  public BackupTableYbc(
      BaseTaskDependencies baseTaskDependencies,
      YbcClientService ybcService,
      YbcBackupUtil ybcBackupUtil,
      YbcManager ybcManager,
      TaskExecutor taskExecutor) {
    super(baseTaskDependencies, ybcService, ybcBackupUtil);
    this.ybcManager = ybcManager;
    this.taskExecutor = taskExecutor;
  }

  public static class Params extends BackupTableParams {
    public Params(BackupTableParams tableParams, YbcBackupNodeRetriever nodeRetriever) {
      super(tableParams);
      this.nodeRetriever = nodeRetriever;
    }
    // Node-ip to use as co-ordinator for the backup.
    public String nodeIp = null;
    public String taskID = null;
    @JsonIgnore public YbcBackupNodeRetriever nodeRetriever;
  }

  @Override
  public BackupTableYbc.Params taskParams() {
    return (BackupTableYbc.Params) taskParams;
  }

  @Override
  public void run() {
    try {
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
      BackupRequestParams backupRequestParams =
          Json.fromJson(taskInfo.getTaskDetails(), BackupRequestParams.class);

      // Wait on node-ip
      if (StringUtils.isBlank(taskParams().nodeIp)) {
        taskParams().nodeIp = taskParams().nodeRetriever.getNodeIpForBackup();
      }
      // Ping operation is attempted again, but it's OK, since a small check only.
      ybcClient = ybcManager.getYbcClient(taskParams().universeUUID, taskParams().nodeIp);

      if (StringUtils.isBlank(taskParams().taskID)) {
        taskParams().taskID =
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
          String dsmTaskId =
              taskParams().taskID.concat(YbcBackupUtil.YBC_SUCCESS_MARKER_TASK_SUFFIX);
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
            Backup.BackupUpdater bUpdater =
                b -> {
                  BackupTableParams tableParams =
                      Backup.getBackupTableParamsFromKeyspaceOrNull(b, taskParams().getKeyspace());
                  if (tableParams != null) {
                    tableParams.thisBackupSubTaskStartTime = (new Date()).getTime();
                  }
                };
            Backup.saveDetails(taskParams().customerUuid, taskParams().backupUuid, bUpdater);
            if (isResumable) {
              backupRequestParams
                  .backupDBStates
                  .get(taskParams().getKeyspace())
                  .setIntermediate(taskParams().nodeIp, taskParams().taskID);
              getRunnableTask().setTaskDetails(Json.toJson(backupRequestParams));
            }
            log.info(
                String.format(
                    "%s Successfully submitted backup task to YB-controller server: %s "
                        + "with taskID: %s",
                    baseLogMessage, taskParams().nodeIp, taskParams().taskID));
          } else if (response.getStatus().getCode().equals(ControllerStatus.EXISTS)) {
            log.info(
                String.format(
                    "%s Already present on YB-controller with taskID: %s",
                    baseLogMessage, taskParams().taskID));
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

      // Poll create backup progress on yb-controller and handle result
      try {
        pollTaskProgress(ybcClient, taskParams().taskID);
        handleBackupResult();
        if (isResumable) {
          backupRequestParams.backupDBStates.get(taskParams().getKeyspace()).resetOnComplete();
          getRunnableTask().setTaskDetails(Json.toJson(backupRequestParams));
        }
        ybcManager.deleteYbcBackupTask(taskParams().universeUUID, taskParams().taskID, ybcClient);
        taskParams().nodeRetriever.putNodeIPBackToPool(taskParams().nodeIp);
      } catch (Exception e) {
        log.error(
            "{} Polling backup task progress on YB-Controller failed with error {}",
            baseLogMessage,
            e.getMessage());
        Throwables.propagate(e);
      }
    } catch (CancellationException ce) {
      if (!taskExecutor.isShutdown()) {
        if (ce.getMessage().contains("Task aborted on YB-Controller")) {
          // Remove task on YB-Controller server.
          ybcManager.deleteYbcBackupTask(taskParams().universeUUID, taskParams().taskID, ybcClient);
        } else {
          // Explicit abort sent to YB-Controller server to abort process there.
          if (StringUtils.isNotBlank(taskParams().taskID)) {
            ybcManager.abortBackupTask(
                taskParams().customerUuid, taskParams().backupUuid, taskParams().taskID, ybcClient);
            ybcManager.deleteYbcBackupTask(
                taskParams().universeUUID, taskParams().taskID, ybcClient);
          }
        }
      }
      Throwables.propagate(new CancellationException(ce.getMessage()));
    } catch (Throwable e) {
      // Backup state will be set to Failed in main task.
      if (StringUtils.isNotBlank(taskParams().taskID)) {
        // Try abort on YB-Controller server.
        ybcManager.abortBackupTask(
            taskParams().customerUuid, taskParams().backupUuid, taskParams().taskID, ybcClient);
        ybcManager.deleteYbcBackupTask(taskParams().universeUUID, taskParams().taskID, ybcClient);
      }
      Throwables.propagate(e);
    } finally {
      try {
        ybcService.closeClient(ybcClient);
      } catch (Exception e) {
      }
    }
  }

  /** Update backup object with success metadata */
  private void handleBackupResult() throws PlatformServiceException {
    BackupServiceTaskResultRequest backupServiceTaskResultRequest =
        ybcBackupUtil.createYbcBackupResultRequest(taskParams().taskID);
    BackupServiceTaskResultResponse backupServiceTaskResultResponse =
        ybcClient.backupServiceTaskResult(backupServiceTaskResultRequest);
    if (backupServiceTaskResultResponse.getTaskStatus().equals(ControllerStatus.OK)) {
      YbcBackupUtil.YbcBackupResponse response =
          ybcBackupUtil.parseYbcBackupResponse(backupServiceTaskResultResponse.getMetadataJson());
      long backupSize = Long.parseLong(response.backupSize);
      Backup.BackupUpdater bUpdater =
          b -> {
            BackupTableParams tableParams =
                Backup.getBackupTableParamsFromKeyspaceOrNull(b, taskParams().getKeyspace());
            if (tableParams != null) {
              tableParams.backupSizeInBytes = backupSize;
              tableParams.timeTakenPartial = backupServiceTaskResultResponse.getTimeTakenMs();
              // Add specific storage locations for regional backups
              if (MapUtils.isNotEmpty(response.responseCloudStoreSpec.regionLocations)) {
                tableParams.regionLocations =
                    ybcBackupUtil.extractRegionLocationFromMetadata(
                        response.responseCloudStoreSpec.regionLocations, taskParams());
              }
            }
          };
      Backup.saveDetails(taskParams().customerUuid, taskParams().backupUuid, bUpdater);
    } else {
      throw new PlatformServiceException(
          backupServiceTaskResultResponse.getTaskStatus().getNumber(),
          String.format(
              "%s YB-controller returned non-zero exit status %s",
              baseLogMessage, backupServiceTaskResultResponse.getTaskStatus().name()));
    }
  }
}
