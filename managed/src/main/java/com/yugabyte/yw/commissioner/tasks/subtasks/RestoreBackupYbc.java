package com.yugabyte.yw.commissioner.tasks.subtasks;

import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import java.util.concurrent.CancellationException;

import com.google.api.client.util.Throwables;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.yugabyte.yw.commissioner.YbcTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.YbcBackupUtil;
import com.yugabyte.yw.common.YbcManager;
import com.yugabyte.yw.common.YbcBackupUtil.YbcBackupResponse;
import com.yugabyte.yw.common.services.YbcClientService;
import com.yugabyte.yw.forms.RestoreBackupParams;
import com.yugabyte.yw.forms.RestoreBackupParams.BackupStorageInfo;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.UUID;
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

  @Inject
  public RestoreBackupYbc(
      BaseTaskDependencies baseTaskDependencies,
      YbcClientService ybcService,
      YbcBackupUtil ybcBackupUtil,
      YbcManager ybcManager) {
    super(baseTaskDependencies, ybcService, ybcBackupUtil);
    this.ybcManager = ybcManager;
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
      restoreBackupParams = Json.fromJson(taskInfo.getTaskDetails(), RestoreBackupParams.class);
      taskId = restoreBackupParams.currentYbcTaskId;
    }

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
    } catch (CancellationException ce) {
    } catch (Throwable e) {
      log.error(String.format("Failed with error %s", e.getMessage()));
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
