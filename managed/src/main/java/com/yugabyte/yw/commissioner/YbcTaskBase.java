// Copyright (c) YugabyteDB, Inc.

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

  public void pollTaskProgress(YbcClient ybcClient, String taskId, String nodeIp) {

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
              baseLogMessage, backupServiceTaskProgressResponse.getTaskStatus(), nodeIp);
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
  private void handleTaskCompleteStage(
      String baseLogMessage, ControllerStatus taskStatus, String nodeIp) {
    // Base log message for statuses that require checking logs on orchestrator YBC node.
    String nodeLogsErr =
        String.format("Please check YB-Controller logs on node %s for more details", nodeIp);
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
      case CREATE_SNAPSHOT_RPC_TIMEOUT:
      case EXPORT_SNAPSHOT_RPC_TIMEOUT:
      case IMPORT_SNAPSHOT_RPC_TIMEOUT:
      case RESTORE_SNAPSHOT_RPC_TIMEOUT:
      case DELETE_SNAPSHOT_RPC_TIMEOUT:
      case WAIT_FOR_TABLETS_SPLIT_RPC_TIMEOUT:
      case LIST_TABLETS_RPC_TIMEOUT:
      case DISABLE_TABLET_SPLITTING_RPC_TIMEOUT:
        throw new RuntimeException(
            String.format(
                "%s Task failed due to RPC timeout status %s. %s",
                baseLogMessage, taskStatus.name(), nodeLogsErr));
      case CREATE_SNAPSHOT_RPC_FAILED:
      case EXPORT_SNAPSHOT_RPC_FAILED:
      case IMPORT_SNAPSHOT_RPC_FAILED:
      case RESTORE_SNAPSHOT_RPC_FAILED:
      case DELETE_SNAPSHOT_RPC_FAILED:
      case WAIT_FOR_TABLETS_SPLIT_RPC_FAILED:
      case LIST_TABLETS_RPC_FAILED:
        throw new RuntimeException(
            String.format(
                "%s Task failed due to failed RPC status %s. %s",
                baseLogMessage, taskStatus.name(), nodeLogsErr));
      case TOO_MANY_CATALOG_VERSION_CHANGES:
        throw new RuntimeException(
            String.format(
                "%s Task failed trying to get a consistent YSQL Dump with status %s. There were too"
                    + " many DDL changes. %s",
                baseLogMessage, taskStatus.name(), nodeLogsErr));
      case YSQL_DUMP_COMMAND_FAILED:
      case MODIFY_YSQL_DUMP_FAILED:
      case CATALOG_VERSION_COMMAND_FAILED:
        throw new RuntimeException(
            String.format(
                "%s Task failed during YSQL Dump phase with status %s. %s",
                baseLogMessage, taskStatus.name(), nodeLogsErr));
      case FETCH_TABLESPACES_COMMAND_FAILED:
        throw new RuntimeException(
            String.format(
                "%s Task failed when trying to fetch Tablespaces info with status %s. %s",
                baseLogMessage, taskStatus.name(), nodeLogsErr));
      default:
        throw new PlatformServiceException(
            taskStatus.getNumber(),
            String.format(
                "%s Failed with status %s. Please check YB-Controller logs for more details.",
                baseLogMessage, taskStatus.name()));
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
