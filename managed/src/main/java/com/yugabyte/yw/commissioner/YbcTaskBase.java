// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.YbcBackupUtil;
import com.yugabyte.yw.common.services.YbcClientService;
import com.yugabyte.yw.forms.UniverseTaskParams;
import java.time.Duration;
import java.util.concurrent.CancellationException;

import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.YbcClient;
import org.yb.ybc.BackupServiceTaskProgressRequest;
import org.yb.ybc.BackupServiceTaskProgressResponse;
import org.yb.ybc.BackupServiceTaskStage;
import org.yb.ybc.ControllerStatus;

@Slf4j
public abstract class YbcTaskBase extends AbstractTaskBase {

  public final YbcClientService ybcService;
  public final YbcBackupUtil ybcBackupUtil;

  // Time to wait (in millisec) between each poll to ybc.
  private static final int WAIT_EACH_ATTEMPT_MS = 15000;
  private static final int MAX_TASK_RETRIES = 10;

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
    String baseLogMessage = String.format("Task id %s status", taskId);
    boolean retriesExhausted = false;
    while (true) {
      BackupServiceTaskProgressResponse backupServiceTaskProgressResponse =
          ybcClient.backupServiceTaskProgress(backupServiceTaskProgressRequest);

      switch (backupServiceTaskProgressResponse.getTaskStatus()) {
        case NOT_STARTED:
          log.info(String.format("%s %s", baseLogMessage, ControllerStatus.NOT_STARTED.toString()));
          break;
        case IN_PROGRESS:
          logProgressResponse(baseLogMessage, backupServiceTaskProgressResponse);
          break;
        case COMPLETE:
        case OK:
        case NOT_FOUND:
          log.info(String.format("%s task complete.", baseLogMessage));
          return;
        case ABORT:
          log.info(String.format("%s task aborted on YB-Controller.", baseLogMessage));
          throw new CancellationException("Yb-Controller task aborted.");
        default:
          if (!retriesExhausted
              && backupServiceTaskProgressResponse.getRetryCount() <= MAX_TASK_RETRIES) {
            log.info(
                "{} Number of retries {}",
                baseLogMessage,
                backupServiceTaskProgressResponse.getRetryCount());
            log.error(
                "{} Last error message: {}",
                baseLogMessage,
                backupServiceTaskProgressResponse.getTaskStatus().name());
            retriesExhausted =
                (backupServiceTaskProgressResponse.getRetryCount() >= MAX_TASK_RETRIES);
            waitFor(Duration.ofMillis(WAIT_EACH_ATTEMPT_MS));
            break;
          }
          throw new PlatformServiceException(
              backupServiceTaskProgressResponse.getTaskStatus().getNumber(),
              String.format(
                  "%s Failed with error %s",
                  baseLogMessage, backupServiceTaskProgressResponse.getTaskStatus().name()));
      }
      waitFor(Duration.ofMillis(WAIT_EACH_ATTEMPT_MS));
    }
  }

  /**
   * Logging progress response for UPLOAD stage
   *
   * @param progressResponse
   */
  private void logProgressResponse(
      String baseLogMessage, BackupServiceTaskProgressResponse progressResponse) {
    BackupServiceTaskStage taskStage = progressResponse.getStage();

    log.info("{} Number of retries {}", baseLogMessage, progressResponse.getRetryCount());

    log.info("{} Current task stage - {}", baseLogMessage, taskStage.name());

    if (taskStage.equals(BackupServiceTaskStage.UPLOAD)) {
      log.info(
          "{} {} ops completed out of {} total",
          baseLogMessage,
          progressResponse.getCompletedOps(),
          progressResponse.getTotalOps());
      log.info("{} {} bytes transferred", baseLogMessage, progressResponse.getBytesTransferred());
    }

    if (taskStage.equals(BackupServiceTaskStage.TASK_COMPLETE)) {
      log.info("{} Task is complete.", baseLogMessage);
    }
  }
}
