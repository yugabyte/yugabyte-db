package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.google.api.client.util.Throwables;
import com.yugabyte.yw.commissioner.YbcTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.YbcBackupUtil;
import com.yugabyte.yw.common.services.YbcClientService;
import com.yugabyte.yw.forms.RestoreBackupParams;
import com.yugabyte.yw.forms.RestoreBackupParams.ActionType;
import com.yugabyte.yw.forms.RestoreBackupParams.BackupStorageInfo;
import com.yugabyte.yw.models.configs.CustomerConfig;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.YbcClient;
import org.yb.CommonTypes.TableType;
import org.yb.ybc.BackupServiceTaskCreateRequest;
import org.yb.ybc.BackupServiceTaskCreateResponse;
import org.yb.ybc.BackupServiceTaskProgressRequest;
import org.yb.ybc.BackupServiceTaskProgressResponse;
import org.yb.ybc.BackupServiceTaskResultRequest;
import org.yb.ybc.BackupServiceTaskResultResponse;
import org.yb.ybc.BackupServiceTaskStage;
import org.yb.ybc.ControllerStatus;

@Slf4j
public class RestoreBackupYbc extends YbcTaskBase {

  private YbcClient ybcClient;

  @Inject
  public RestoreBackupYbc(
      BaseTaskDependencies baseTaskDependencies,
      YbcClientService ybcService,
      YbcBackupUtil ybcBackupUtil) {
    super(baseTaskDependencies, ybcService, ybcBackupUtil);
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
      Throwables.propagate(e);
    }
    String taskId = null;
    // Send create restore to yb-controller
    try {

      BackupStorageInfo backupStorageInfo = taskParams().backupStorageInfoList.get(0);
      taskId =
          userTaskUUID.toString()
              + "_"
              + backupStorageInfo.backupType.toString()
              + "_"
              + backupStorageInfo.keyspace;
      BackupServiceTaskCreateRequest restoreTaskCreateRequest =
          ybcBackupUtil.createYbcRestoreRequest(taskParams(), backupStorageInfo, taskId);
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

      try {
        pollTaskProgress(ybcClient, taskId);
        handleBackupResult(taskId);
      } catch (PlatformServiceException e) {
        log.error(String.format("Failed with error %s", e.getMessage()));
        ybcService.closeClient(ybcClient);
        Throwables.propagate(e);
      }

    } catch (Exception e) {
      log.error(String.format("Failed with error %s", e.getMessage()));
      ybcService.closeClient(ybcClient);
      Throwables.propagate(e);
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
