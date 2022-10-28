package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.models.Restore;
import com.yugabyte.yw.models.RestoreKeyspace;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import java.util.concurrent.CancellationException;

import com.google.api.client.util.Throwables;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.TaskExecutor;
import com.yugabyte.yw.commissioner.YbcTaskBase;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.YbcBackupUtil;
import com.yugabyte.yw.common.YbcManager;
import com.yugabyte.yw.common.YbcBackupUtil.YbcBackupResponse;
import com.yugabyte.yw.common.services.YbcClientService;
import com.yugabyte.yw.forms.RestoreBackupParams;
import com.yugabyte.yw.forms.RestoreBackupParams.BackupStorageInfo;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.Optional;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.YbcClient;
import org.apache.commons.lang.StringUtils;
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
    // Node-ip to use as co-ordinator for the restore.
    public String nodeIp = null;

    public int index;

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
    try {
      ybcClient = ybcBackupUtil.getYbcClient(taskParams().universeUUID, taskParams().nodeIp);
    } catch (PlatformServiceException e) {
      log.error("Could not generate YB-Controller client, error: %s", e.getMessage());
      Throwables.propagate(e);
    }
    BackupStorageInfo backupStorageInfo = taskParams().backupStorageInfoList.get(0);

    TaskInfo taskInfo = TaskInfo.getOrBadRequest(userTaskUUID);
    boolean isResumable = false;
    if (taskInfo.getTaskType().equals(TaskType.RestoreBackup)) {
      isResumable = true;
    }
    ObjectMapper mapper = new ObjectMapper();
    RestoreBackupParams restoreBackupParams = null;
    JsonNode restoreParams = null;
    String taskId = null;
    if (isResumable) {
      restoreBackupParams = Json.fromJson(getTaskDetails(), RestoreBackupParams.class);
      taskId = restoreBackupParams.currentYbcTaskId;
    }

    restoreBackupParams = Json.fromJson(taskInfo.getTaskDetails(), RestoreBackupParams.class);
    Optional<RestoreKeyspace> restoreKeyspaceIfPresent =
        RestoreKeyspace.fetchRestoreKeyspaceByRestoreIdAndKeyspaceName(
            restoreBackupParams.prefixUUID, backupStorageInfo.keyspace);
    RestoreKeyspace restoreKeyspace = null;
    boolean updateRestoreSizeInBytes = true;
    if (!restoreKeyspaceIfPresent.isPresent()) {
      log.info("Creating entry for restore keyspace: {}", taskUUID);
      restoreKeyspace = RestoreKeyspace.create(TaskInfo.getOrBadRequest(taskUUID));
    } else {
      restoreKeyspace = restoreKeyspaceIfPresent.get();
      restoreKeyspace.updateTaskUUID(taskUUID);
      updateRestoreSizeInBytes = false;
      restoreKeyspace.update(taskUUID, TaskInfo.State.Running);
    }
    long backupSize = 0L;
    // Send create restore to yb-controller
    try {
      if (taskId == null) {
        taskId =
            ybcBackupUtil.getYbcTaskID(
                taskParams().prefixUUID,
                backupStorageInfo.backupType.toString(),
                backupStorageInfo.keyspace);
        try {
          String dsmTaskId = taskId.concat(YbcBackupUtil.YBC_SUCCESS_MARKER_TASK_SUFFIX);
          BackupServiceTaskCreateRequest downloadSuccessMarkerRequest =
              ybcBackupUtil.createDsmRequest(
                  taskParams().customerUUID,
                  taskParams().storageConfigUUID,
                  dsmTaskId,
                  backupStorageInfo);
          String successMarkerString =
              ybcManager.downloadSuccessMarker(
                  downloadSuccessMarkerRequest, taskParams().universeUUID, dsmTaskId);
          if (StringUtils.isEmpty(successMarkerString)) {
            throw new PlatformServiceException(
                INTERNAL_SERVER_ERROR, "Got empty success marker response, exiting.");
          }
          YbcBackupResponse successMarker =
              ybcBackupUtil.parseYbcBackupResponse(successMarkerString);
          backupSize = Long.parseLong(successMarker.backupSize);
          BackupServiceTaskCreateRequest restoreTaskCreateRequest =
              ybcBackupUtil.createYbcRestoreRequest(
                  taskParams().customerUUID,
                  taskParams().storageConfigUUID,
                  backupStorageInfo,
                  taskId,
                  successMarker);
          ybcBackupUtil.validateConfigWithSuccessMarker(
              successMarker, restoreTaskCreateRequest.getCsConfig(), false);
          BackupServiceTaskCreateResponse response =
              ybcClient.restoreNamespace(restoreTaskCreateRequest);
          if (response.getStatus().getCode().equals(ControllerStatus.OK)) {
            log.info(String.format("Successfully submitted restore task to YB-controller"));
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
        restoreParams = Json.toJson(restoreBackupParams);
        getRunnableTask().setTaskDetails(restoreParams);
      }

      try {
        pollTaskProgress(ybcClient, taskId);
        handleBackupResult(taskId);
        if (isResumable) {
          restoreBackupParams.currentYbcTaskId = null;
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
        Restore.updateRestoreSizeForRestore(taskUUID, backupSize);
      }
      restoreKeyspace.update(taskUUID, TaskInfo.State.Success);
    } catch (CancellationException ce) {
      if (!taskExecutor.isShutdown()) {
        // update aborted/failed - not showing aborted from here.
        if (restoreKeyspace != null) {
          restoreKeyspace.update(taskUUID, TaskInfo.State.Aborted);
        }
        ybcManager.deleteYbcBackupTask(taskParams().universeUUID, taskId, ybcClient);
      }
      Throwables.propagate(ce);
    } catch (Throwable e) {
      log.error(String.format("Failed with error %s", e.getMessage()));
      if (restoreKeyspace != null) {
        restoreKeyspace.update(taskUUID, TaskInfo.State.Failure);
      }
      if (StringUtils.isNotBlank(taskId)) {
        ybcManager.deleteYbcBackupTask(taskParams().universeUUID, taskId, ybcClient);
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
