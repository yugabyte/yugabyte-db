// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import play.libs.Json;

import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.api.client.util.Throwables;
import com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.TaskExecutor.SubTaskGroup;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.YbcBackupUtil;
import com.yugabyte.yw.common.YbcManager;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.common.services.YbcClientService;
import com.yugabyte.yw.controllers.handlers.UniverseInfoHandler;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Backup.StorageConfigType;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageWithRegionsData;
import java.time.Duration;
import java.util.Date;
import java.util.UUID;
import java.util.concurrent.CancellationException;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections.CollectionUtils;
import org.apache.commons.collections.MapUtils;
import org.yb.client.YBClient;
import org.yb.client.YbcClient;
import org.yb.ybc.BackupServiceTaskCreateRequest;
import org.yb.ybc.BackupServiceTaskCreateResponse;
import org.yb.ybc.BackupServiceTaskProgressRequest;
import org.yb.ybc.BackupServiceTaskProgressResponse;
import org.yb.ybc.BackupServiceTaskResultRequest;
import org.yb.ybc.BackupServiceTaskResultResponse;
import org.yb.ybc.BackupServiceTaskStage;
import org.yb.ybc.ControllerStatus;

@Slf4j
public class BackupTableYbc extends AbstractTaskBase {

  private final YbcClientService ybcService;
  private final YbcBackupUtil ybcBackupUtil;
  private final YbcManager ybcManager;
  private YbcClient ybcClient;
  private long totalTimeTaken = 0L;
  private long totalSizeinBytes = 0L;
  private String baseLogMessage = null;
  private String taskID = null;

  // Time to wait (in millisec) between each poll to ybc.
  private static final int WAIT_EACH_ATTEMPT_MS = 15000;
  private static final int MAX_TASK_RETRIES = 10;

  @Inject
  public BackupTableYbc(
      BaseTaskDependencies baseTaskDependencies,
      YbcClientService ybcService,
      YbcBackupUtil ybcBackupUtil,
      YbcManager ybcManager) {
    super(baseTaskDependencies);
    this.ybcService = ybcService;
    this.ybcBackupUtil = ybcBackupUtil;
    this.ybcManager = ybcManager;
  }

  @Override
  public BackupTableParams taskParams() {
    return (BackupTableParams) taskParams;
  }

  @Override
  public void run() {
    int idx = 0;
    try {
      for (BackupTableParams tableParams : taskParams().backupList) {
        baseLogMessage =
            ybcBackupUtil.getBaseLogMessage(tableParams.backupUuid, tableParams.getKeyspace());
        taskID = ybcBackupUtil.getYbcTaskID(tableParams.backupUuid, tableParams.getKeyspace());
        ybcClient = ybcBackupUtil.getYbcClient(tableParams.universeUUID);
        try {
          // Send create backup request to yb-controller
          BackupServiceTaskCreateRequest backupServiceTaskCreateRequest =
              ybcBackupUtil.createYbcBackupRequest(tableParams);
          BackupServiceTaskCreateResponse response =
              ybcClient.backupNamespace(backupServiceTaskCreateRequest);
          if (response.getStatus().getCode().equals(ControllerStatus.OK)) {
            log.info(
                String.format(
                    "%s Successfully submitted backup task to YB-controller with taskID: %s",
                    baseLogMessage, taskID));
          } else {
            throw new PlatformServiceException(
                response.getStatus().getCodeValue(),
                String.format(
                    "%s YB-controller returned non-zero exit status %s",
                    baseLogMessage, response.getStatus().getErrorMessage()));
          }
          // Poll create backup progress on yb-controller and handle result
          pollTaskProgress(tableParams);
          handleBackupResult(tableParams, idx);
        } catch (Exception e) {
          log.error("{} Failed with error {}", baseLogMessage, e.getMessage());
          Throwables.propagate(e);
        }
        ybcManager.deleteYbcBackupTask(tableParams.universeUUID, taskID);
        idx++;
      }
      Backup backup = Backup.getOrBadRequest(taskParams().customerUuid, taskParams().backupUuid);
      backup.setCompletionTime(new Date(backup.getCreateTime().getTime() + totalTimeTaken));
      backup.setTotalBackupSize(totalSizeinBytes);
      backup.transitionState(Backup.BackupState.Completed);
    } catch (CancellationException ce) {
      ybcManager.abortBackupTask(taskParams().customerUuid, taskParams().backupUuid, taskID);
      // Backup stopped state will be updated in the main createBackup task
      Throwables.propagate(ce);
    } catch (Exception e) {
      ybcManager.deleteYbcBackupTask(taskParams().universeUUID, taskID);
      Backup backup = Backup.getOrBadRequest(taskParams().customerUuid, taskParams().backupUuid);
      backup.transitionState(Backup.BackupState.Failed);
      Throwables.propagate(e);
    } finally {
      if (ybcClient != null) {
        ybcClient.close();
      }
    }
  }

  /**
   * Periodically poll task progress for create backup
   *
   * @param tableParams
   */
  private void pollTaskProgress(BackupTableParams tableParams) {

    BackupServiceTaskProgressRequest backupServiceTaskProgressRequest =
        ybcBackupUtil.createYbcBackupTaskProgressRequest(taskID);
    boolean retriesExhausted = false;
    while (true) {
      BackupServiceTaskProgressResponse backupServiceTaskProgressResponse =
          ybcClient.backupServiceTaskProgress(backupServiceTaskProgressRequest);

      switch (backupServiceTaskProgressResponse.getTaskStatus()) {
        case NOT_STARTED:
          log.info(String.format("%s %s", baseLogMessage, ControllerStatus.NOT_STARTED.toString()));
          break;
        case IN_PROGRESS:
          logProgressResponse(backupServiceTaskProgressResponse);
          break;
        case COMPLETE:
        case OK:
        case NOT_FOUND:
          log.info(String.format("%s task complete.", baseLogMessage));
          return;
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
  private void logProgressResponse(BackupServiceTaskProgressResponse progressResponse) {
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

  /**
   * Update backup object with success metadata
   *
   * @param tableParams
   * @param idx
   */
  private void handleBackupResult(BackupTableParams tableParams, int idx) {
    BackupServiceTaskResultRequest backupServiceTaskResultRequest =
        ybcBackupUtil.createYbcBackupResultRequest(taskID);
    BackupServiceTaskResultResponse backupServiceTaskResultResponse =
        ybcClient.backupServiceTaskResult(backupServiceTaskResultRequest);
    if (backupServiceTaskResultResponse.getTaskStatus().equals(ControllerStatus.OK)) {
      Backup backup = Backup.getOrBadRequest(taskParams().customerUuid, taskParams().backupUuid);
      YbcBackupUtil.YbcBackupResponse response =
          ybcBackupUtil.parseYbcBackupResponse(backupServiceTaskResultResponse.getMetadataJson());
      long backupSize = Long.parseLong(response.backupSize);
      backup.setBackupSizeInBackupList(idx, backupSize);
      totalSizeinBytes += backupSize;
      totalTimeTaken += backupServiceTaskResultResponse.getTimeTakenMs();

      // Add specific storage locations for regional backups
      if (MapUtils.isNotEmpty(response.responseCloudStoreSpec.regionLocations)) {
        backup.setPerRegionLocations(
            idx,
            ybcBackupUtil.extractRegionLocationfromMetadata(
                response.responseCloudStoreSpec.regionLocations, tableParams));
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
