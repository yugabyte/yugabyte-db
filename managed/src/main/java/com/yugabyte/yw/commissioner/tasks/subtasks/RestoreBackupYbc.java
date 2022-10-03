package com.yugabyte.yw.commissioner.tasks.subtasks;

import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import java.util.concurrent.CancellationException;

import com.google.api.client.util.Throwables;
import com.yugabyte.yw.commissioner.YbcTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.YbcBackupUtil;
import com.yugabyte.yw.common.YbcManager;
import com.yugabyte.yw.common.YbcBackupUtil.YbcBackupResponse;
import com.yugabyte.yw.common.services.YbcClientService;
import com.yugabyte.yw.forms.RestoreBackupParams;
import com.yugabyte.yw.forms.RestoreBackupParams.BackupStorageInfo;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.YbcClient;
import org.apache.commons.lang.StringUtils;
import org.yb.ybc.BackupServiceTaskCreateRequest;
import org.yb.ybc.BackupServiceTaskCreateResponse;
import org.yb.ybc.BackupServiceTaskResultRequest;
import org.yb.ybc.BackupServiceTaskResultResponse;
import org.yb.ybc.ControllerStatus;

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

  @Override
  public RestoreBackupParams taskParams() {
    return (RestoreBackupParams) taskParams;
  }

  @Override
  public void run() {
    try {
      ybcClient = ybcBackupUtil.getYbcClient(taskParams().universeUUID);
    } catch (PlatformServiceException e) {
      log.error("Could not generate YB-Controller client, error: %s", e.getMessage());
      Throwables.propagate(e);
    }
    String taskId = null;
    // Send create restore to yb-controller
    try {

      BackupStorageInfo backupStorageInfo = taskParams().backupStorageInfoList.get(0);
      taskId =
          ybcBackupUtil.getYbcTaskID(
              userTaskUUID, backupStorageInfo.backupType.toString(), backupStorageInfo.keyspace);
      try {
        BackupServiceTaskCreateRequest downloadSuccessMarkerRequest =
            ybcBackupUtil.createDsmRequest(
                taskParams().customerUUID,
                taskParams().storageConfigUUID,
                taskId,
                backupStorageInfo);
        String successMarkerString =
            ybcManager.downloadSuccessMarker(
                downloadSuccessMarkerRequest, taskParams().universeUUID, taskId);
        if (StringUtils.isEmpty(successMarkerString)) {
          throw new PlatformServiceException(
              INTERNAL_SERVER_ERROR, "Got empty success marker response, exiting.");
        }
        YbcBackupResponse successMarker = ybcBackupUtil.parseYbcBackupResponse(successMarkerString);
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
        } else {
          throw new PlatformServiceException(
              response.getStatus().getCodeValue(),
              String.format(
                  "YB-controller returned non-zero exit status %s",
                  response.getStatus().getErrorMessage()));
        }
      } catch (Exception e) {
        log.error("Sending restore request to YB-Controller failed with error {}", e.getMessage());
        Throwables.propagate(e);
      }

      try {
        pollTaskProgress(ybcClient, taskId);
        handleBackupResult(taskId);
      } catch (Exception e) {
        log.error(
            "Polling restore task progress on YB-Controller failed with error {}", e.getMessage());
        Throwables.propagate(e);
      }
    } catch (Throwable e) {
      log.error(String.format("Failed with error %s", e.getMessage()));
      if (StringUtils.isNotBlank(taskId)) {
        ybcManager.deleteYbcBackupTask(taskParams().universeUUID, taskId);
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
