package com.yugabyte.yw.commissioner.tasks.subtasks;

import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.databind.JsonNode;
import com.google.api.client.util.Throwables;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.TaskExecutor;
import com.yugabyte.yw.commissioner.YbcTaskBase;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.backuprestore.ybc.YbcBackupUtil;
import com.yugabyte.yw.common.backuprestore.ybc.YbcBackupUtil.YbcBackupResponse;
import com.yugabyte.yw.common.backuprestore.ybc.YbcManager;
import com.yugabyte.yw.common.services.YbcClientService;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.forms.RestoreBackupParams;
import com.yugabyte.yw.forms.RestoreBackupParams.BackupStorageInfo;
import com.yugabyte.yw.models.Restore;
import com.yugabyte.yw.models.RestoreKeyspace;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.Optional;
import java.util.concurrent.CancellationException;
import javax.inject.Inject;
import lombok.Getter;
import lombok.Setter;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import org.yb.client.YbcClient;
import org.yb.ybc.BackupServiceTaskCreateRequest;
import org.yb.ybc.BackupServiceTaskCreateResponse;
import org.yb.ybc.BackupServiceTaskResultRequest;
import org.yb.ybc.BackupServiceTaskResultResponse;
import org.yb.ybc.ControllerStatus;
import play.libs.Json;

@Slf4j
public class RestoreBackupYbc extends YbcTaskBase {

  private YbcClient ybcClient;
  private YbcManager ybcManager;
  private TaskExecutor taskExecutor;

  @Inject
  public RestoreBackupYbc(
      BaseTaskDependencies baseTaskDependencies,
      YbcClientService ybcService,
      YbcBackupUtil ybcBackupUtil,
      YbcManager ybcManager,
      TaskExecutor taskExecutor) {
    super(baseTaskDependencies, ybcService, ybcBackupUtil);
    this.ybcManager = ybcManager;
    this.taskExecutor = taskExecutor;
  }

  public static class Params extends RestoreBackupParams {
    public int index;

    @JsonIgnore @Setter @Getter private YbcBackupResponse successMarker;

    public Params(RestoreBackupParams params) {
      super(params);
    }
  }

  @Override
  public RestoreBackupYbc.Params taskParams() {
    return (RestoreBackupYbc.Params) taskParams;
  }

  @Override
  public void run() {
    BackupStorageInfo backupStorageInfo = taskParams().backupStorageInfoList.get(0);

    TaskInfo taskInfo = TaskInfo.getOrBadRequest(userTaskUUID);
    boolean isResumable = false;
    if (taskInfo.getTaskType().equals(TaskType.RestoreBackup)) {
      isResumable = true;
    }
    JsonNode restoreParams = null;
    String taskId = null;
    String nodeIp = null;

    RestoreBackupParams restoreBackupParams =
        Json.fromJson(taskInfo.getDetails(), RestoreBackupParams.class);
    if (isResumable) {
      taskId = restoreBackupParams.currentYbcTaskId;
      nodeIp = restoreBackupParams.nodeIp;
    }

    try {
      if (StringUtils.isBlank(nodeIp)) {
        Pair<YbcClient, String> clientIPPair =
            ybcManager.getAvailableYbcClientIpPair(taskParams().getUniverseUUID(), true);
        ybcClient = clientIPPair.getFirst();
        nodeIp = clientIPPair.getSecond();
      } else {
        ybcClient = ybcManager.getYbcClient(taskParams().getUniverseUUID(), nodeIp);
      }
    } catch (PlatformServiceException e) {
      log.error("Could not generate YB-Controller client, error: %s", e.getMessage());
      Throwables.propagate(e);
    }

    Optional<RestoreKeyspace> restoreKeyspaceIfPresent =
        RestoreKeyspace.fetchRestoreKeyspace(
            restoreBackupParams.prefixUUID,
            backupStorageInfo.keyspace,
            backupStorageInfo.storageLocation);
    RestoreKeyspace restoreKeyspace = null;
    boolean updateRestoreSizeInBytes = true;
    if (!restoreKeyspaceIfPresent.isPresent()) {
      log.info("Creating entry for restore keyspace: {}", taskUUID);
      restoreKeyspace = RestoreKeyspace.create(taskUUID, taskParams());
    } else {
      restoreKeyspace = restoreKeyspaceIfPresent.get();
      restoreKeyspace.updateTaskUUID(taskUUID);
      updateRestoreSizeInBytes = false;
      restoreKeyspace.update(taskUUID, RestoreKeyspace.State.InProgress);
    }
    long backupSize = 0L;
    // Send create restore to yb-controller
    try {
      if (StringUtils.isBlank(taskId)) {
        taskId =
            ybcBackupUtil.getYbcTaskID(
                    taskParams().prefixUUID,
                    backupStorageInfo.backupType.toString(),
                    backupStorageInfo.keyspace)
                + "-"
                + Integer.toString(taskParams().index);
        try {
          // For xcluster task, success marker is empty until task execution
          if (taskParams().getSuccessMarker() == null) {
            String dsmTaskId = taskId.concat(YbcBackupUtil.YBC_SUCCESS_MARKER_TASK_SUFFIX);
            BackupServiceTaskCreateRequest downloadSuccessMarkerRequest =
                ybcBackupUtil.createDsmRequest(
                    taskParams().customerUUID,
                    taskParams().storageConfigUUID,
                    dsmTaskId,
                    backupStorageInfo);
            String successMarkerString =
                ybcManager.downloadSuccessMarker(
                    downloadSuccessMarkerRequest, taskParams().getUniverseUUID(), dsmTaskId);
            if (StringUtils.isEmpty(successMarkerString)) {
              throw new PlatformServiceException(
                  INTERNAL_SERVER_ERROR, "Got empty success marker response, exiting.");
            }
            YbcBackupResponse successMarker =
                YbcBackupUtil.parseYbcBackupResponse(successMarkerString);
            taskParams().setSuccessMarker(successMarker);
          }
          backupSize = Long.parseLong(taskParams().getSuccessMarker().backupSize);
          BackupServiceTaskCreateRequest restoreTaskCreateRequest =
              ybcBackupUtil.createYbcRestoreRequest(
                  taskParams().customerUUID,
                  taskParams().storageConfigUUID,
                  backupStorageInfo,
                  taskId,
                  taskParams().getSuccessMarker());
          YbcBackupUtil.validateConfigWithSuccessMarker(
              taskParams().getSuccessMarker(), restoreTaskCreateRequest.getCsConfig(), false);
          BackupServiceTaskCreateResponse response =
              ybcClient.restoreNamespace(restoreTaskCreateRequest);
          if (response.getStatus().getCode().equals(ControllerStatus.OK)) {
            log.info(
                String.format(
                    "Successfully submitted restore task to YB-controller server: %s"
                        + " with task id: %s",
                    nodeIp, taskId));
          } else if (response.getStatus().getCode().equals(ControllerStatus.EXISTS)) {
            log.info(String.format("Already present on YB-controller with taskID: %s", taskId));
          } else {
            throw new PlatformServiceException(
                response.getStatus().getCodeValue(),
                String.format(
                    "YB-controller returned non-zero exit status %s",
                    response.getStatus().getErrorMessage()));
          }
        } catch (Exception e) {
          log.error(
              "Sending restore request to YB-Controller failed with error {}", e.getMessage());
          Throwables.propagate(e);
        }
      }
      if (isResumable) {
        restoreBackupParams.currentYbcTaskId = taskId;
        restoreBackupParams.currentIdx = taskParams().index;
        restoreBackupParams.nodeIp = nodeIp;
        restoreParams = Json.toJson(restoreBackupParams);
        getRunnableTask().setTaskDetails(restoreParams);
      }

      try {
        pollTaskProgress(ybcClient, taskId);
        handleBackupResult(taskId);
        if (isResumable) {
          restoreBackupParams.currentYbcTaskId = null;
          restoreBackupParams.nodeIp = null;
          restoreBackupParams.currentIdx++;
          restoreParams = Json.toJson(restoreBackupParams);
          getRunnableTask().setTaskDetails(restoreParams);
        }
      } catch (Exception e) {
        log.error(
            "Polling restore task progress on YB-Controller failed with error {}", e.getMessage());
        Throwables.propagate(e);
      }
      if (updateRestoreSizeInBytes) {
        Restore.updateRestoreSizeForRestore(taskParams().prefixUUID, backupSize);
      }
      if (restoreKeyspace != null) {
        restoreKeyspace.update(taskUUID, RestoreKeyspace.State.Completed);
      }
    } catch (CancellationException ce) {
      if (!taskExecutor.isShutdown()) {
        // update aborted/failed - not showing aborted from here.
        if (restoreKeyspace != null) {
          restoreKeyspace.update(taskUUID, RestoreKeyspace.State.Aborted);
        }
        ybcManager.deleteYbcBackupTask(taskParams().getUniverseUUID(), taskId, ybcClient);
      }
      Throwables.propagate(ce);
    } catch (Throwable e) {
      log.error(String.format("Failed with error %s", e.getMessage()));
      if (restoreKeyspace != null) {
        restoreKeyspace.update(taskUUID, RestoreKeyspace.State.Failed);
      }
      if (StringUtils.isNotBlank(taskId)) {
        ybcManager.deleteYbcBackupTask(taskParams().getUniverseUUID(), taskId, ybcClient);
      }
      Throwables.propagate(e);
    } finally {
      if (ybcClient != null) {
        ybcService.closeClient(ybcClient);
      }
    }
  }

  private void handleBackupResult(String taskId) {
    BackupServiceTaskResultRequest backupServiceTaskResultRequest =
        ybcBackupUtil.createYbcBackupResultRequest(taskId);
    BackupServiceTaskResultResponse backupServiceTaskResultResponse =
        ybcClient.backupServiceTaskResult(backupServiceTaskResultRequest);
    if (backupServiceTaskResultResponse.getTaskStatus().equals(ControllerStatus.OK)) {
      return;
    } else {
      throw new PlatformServiceException(
          backupServiceTaskResultResponse.getTaskStatus().getNumber(),
          String.format(
              "YB-controller returned non-zero exit status %s",
              backupServiceTaskResultResponse.getTaskStatus().name()));
    }
  }
}
