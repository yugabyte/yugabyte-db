// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.backuprestore.ybc.YbcBackupUtil;
import com.yugabyte.yw.common.services.YbcClientService;
import com.yugabyte.yw.forms.UniverseTaskParams;
import java.time.Duration;
import java.util.concurrent.CancellationException;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.YbcClient;
import org.yb.ybc.BackupServiceTaskProgressRequest;
import org.yb.ybc.BackupServiceTaskProgressResponse;
import org.yb.ybc.ControllerStatus;

@Slf4j
public abstract class YbcTaskBase extends AbstractTaskBase {

  public final YbcClientService ybcService;
  public final YbcBackupUtil ybcBackupUtil;

  // Time to wait (in millisec) between each poll to ybc.
  private final int WAIT_EACH_ATTEMPT_MS = 15000;

  @Inject
  public YbcTaskBase(
      BaseTaskDependencies baseTaskDependencies,
      YbcClientService ybcService,
      YbcBackupUtil ybcBackupUtil) {
    super(baseTaskDependencies);
    this.ybcService = ybcService;
    this.ybcBackupUtil = ybcBackupUtil;
  }

  @Override
  public UniverseTaskParams taskParams() {
    return (UniverseTaskParams) taskParams;
  }

  public void pollTaskProgress(YbcClient ybcClient, String taskId) {

    BackupServiceTaskProgressRequest backupServiceTaskProgressRequest =
        ybcBackupUtil.createYbcBackupTaskProgressRequest(taskId);
    String baseLogMessage = String.format("Task id %s status:", taskId);
    while (true) {
      BackupServiceTaskProgressResponse backupServiceTaskProgressResponse =
          ybcClient.backupServiceTaskProgress(backupServiceTaskProgressRequest);

      if (backupServiceTaskProgressResponse == null) {
        throw new RuntimeException(
            String.format("%s %s", baseLogMessage, "Got error checking progress on YB-Controller"));
      }
      log.info(
          "{} Number of retries {}",
          baseLogMessage,
          backupServiceTaskProgressResponse.getRetryCount());

      switch (backupServiceTaskProgressResponse.getStage()) {
        case UPLOAD:
        case DOWNLOAD:
          logProgressResponse(baseLogMessage, backupServiceTaskProgressResponse);
          break;
        case TASK_COMPLETE:
          handleTaskCompleteStage(
              baseLogMessage, backupServiceTaskProgressResponse.getTaskStatus());
          return;
        default:
          log.info(
              "{} Current task stage - {}",
              baseLogMessage,
              backupServiceTaskProgressResponse.getStage().name());
      }
      waitFor(Duration.ofMillis(WAIT_EACH_ATTEMPT_MS));
    }
  }

  /** Handle Controller status on task stage TASK_COMPLETE */
  private void handleTaskCompleteStage(String baseLogMessage, ControllerStatus taskStatus) {
    switch (taskStatus) {
      case COMPLETE:
      case OK:
        log.info(String.format("%s Task complete.", baseLogMessage));
        return;
      case ABORT:
        log.info(String.format("%s Task aborted on YB-Controller.", baseLogMessage));
        throw new CancellationException("Task aborted on YB-Controller.");
      case NOT_FOUND:
        throw new RuntimeException(
            String.format("%s %s", baseLogMessage, "Task not found on YB-Controller"));
      default:
        throw new PlatformServiceException(
            taskStatus.getNumber(),
            String.format("%s Failed with error %s", baseLogMessage, taskStatus.name()));
    }
  }

  /**
   * Logging progress response for UPLOAD/DOWNLOAD stage
   *
   * @param progressResponse
   */
  private void logProgressResponse(
      String baseLogMessage, BackupServiceTaskProgressResponse progressResponse) {
    log.info("{} Current task stage - {}", baseLogMessage, progressResponse.getStage());
    log.info(
        "{} {} ops completed out of {} total",
        baseLogMessage,
        progressResponse.getCompletedOps(),
        progressResponse.getTotalOps());
    log.info("{} {} bytes transferred", baseLogMessage, progressResponse.getBytesTransferred());
  }
}
