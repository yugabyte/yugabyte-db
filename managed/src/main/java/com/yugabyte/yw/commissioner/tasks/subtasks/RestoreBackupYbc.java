package com.yugabyte.yw.commissioner.tasks.subtasks;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.api.client.util.Throwables;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.TaskExecutor;
import com.yugabyte.yw.commissioner.YbcTaskBase;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.backuprestore.ybc.YbcBackupUtil;
import com.yugabyte.yw.common.backuprestore.ybc.YbcBackupUtil.YbcBackupResponse;
import com.yugabyte.yw.common.backuprestore.ybc.YbcManager;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.services.YbcClientService;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.forms.RestoreBackupParams;
import com.yugabyte.yw.forms.RestoreBackupParams.BackupStorageInfo;
import com.yugabyte.yw.models.Restore;
import com.yugabyte.yw.models.RestoreKeyspace;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import java.io.IOException;
import java.util.Optional;
import java.util.Set;
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

    TaskInfo taskInfo = TaskInfo.getOrBadRequest(getUserTaskUUID());
    boolean isResumable = false;
    if (taskInfo.getTaskType().equals(TaskType.RestoreBackup)) {
      isResumable = true;
    }
    JsonNode restoreParams = null;
    String taskId = null;
    String nodeIp = null;

    RestoreBackupParams restoreBackupParams =
        Json.fromJson(taskInfo.getTaskParams(), RestoreBackupParams.class);
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
      log.info("Creating entry for restore keyspace: {}", getTaskUUID());
      restoreKeyspace = RestoreKeyspace.create(getTaskUUID(), taskParams());
    } else {
      restoreKeyspace = restoreKeyspaceIfPresent.get();
      restoreKeyspace.updateTaskUUID(getTaskUUID());
      updateRestoreSizeInBytes = false;
      restoreKeyspace.update(getTaskUUID(), RestoreKeyspace.State.InProgress);
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
                    backupStorageInfo,
                    taskParams().getUniverseUUID());
            String successMarkerString =
                ybcManager.downloadSuccessMarker(
                    downloadSuccessMarkerRequest, dsmTaskId, ybcClient);
            if (StringUtils.isEmpty(successMarkerString)) {
              throw new PlatformServiceException(
                  INTERNAL_SERVER_ERROR, "Got empty success marker response, exiting.");
            }
            YbcBackupResponse successMarker =
                YbcBackupUtil.parseYbcBackupResponse(successMarkerString);
            taskParams().setSuccessMarker(successMarker);
          }

          Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
          if (!confGetter.getConfForScope(
              universe, UniverseConfKeys.skipBackupMetadataValidation)) {
            validateBackupMetadata(universe);
          }

          backupSize = Long.parseLong(taskParams().getSuccessMarker().backupSize);
          BackupServiceTaskCreateRequest restoreTaskCreateRequest =
              ybcBackupUtil.createYbcRestoreRequest(
                  taskParams().customerUUID,
                  taskParams().storageConfigUUID,
                  backupStorageInfo,
                  taskId,
                  taskParams().getSuccessMarker(),
                  taskParams().getUniverseUUID());
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
        getRunnableTask().setTaskParams(restoreParams);
      }

      try {
        pollTaskProgress(ybcClient, taskId);
        handleBackupResult(taskId);
        if (isResumable) {
          restoreBackupParams.currentYbcTaskId = null;
          restoreBackupParams.nodeIp = null;
          restoreBackupParams.currentIdx++;
          restoreParams = Json.toJson(restoreBackupParams);
          getRunnableTask().setTaskParams(restoreParams);
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
        restoreKeyspace.update(getTaskUUID(), RestoreKeyspace.State.Completed);
      }
    } catch (CancellationException ce) {
      if (!taskExecutor.isShutdown()) {
        // update aborted/failed - not showing aborted from here.
        if (restoreKeyspace != null) {
          restoreKeyspace.update(getTaskUUID(), RestoreKeyspace.State.Aborted);
        }
        ybcManager.deleteYbcBackupTask(taskId, ybcClient);
      }
      Throwables.propagate(ce);
    } catch (Throwable e) {
      log.error(String.format("Failed with error %s", e.getMessage()));
      if (restoreKeyspace != null) {
        restoreKeyspace.update(getTaskUUID(), RestoreKeyspace.State.Failed);
      }
      if (StringUtils.isNotBlank(taskId)) {
        ybcManager.deleteYbcBackupTask(taskId, ybcClient);
      }
      Throwables.propagate(e);
    } finally {
      if (ybcClient != null) {
        ybcService.closeClient(ybcClient);
      }
    }
  }

  private void validateBackupMetadata(Universe universe) {
    try {
      String restoreUniverseDBVersion =
          universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
      ObjectMapper mapper = new ObjectMapper();
      String extendedArgs = taskParams().successMarker.extendedArgsString;
      if (StringUtils.isEmpty(extendedArgs)) {
        return;
      }
      YbcBackupUtil.YbcSuccessBackupConfig backupConfig =
          mapper.readValue(
              taskParams().successMarker.extendedArgsString,
              YbcBackupUtil.YbcSuccessBackupConfig.class);
      if (backupConfig == null) {
        return;
      }
      // Skip all DB version checks if the runtime flag "yb.skip_version_checks" is true. User must
      // take care of downgrades and restores.
      if (!confGetter.getGlobalConf(GlobalConfKeys.skipVersionChecks)) {
        // Do the following DB version checks only if both the source universe version or target
        // universe version are part of stable or preview. Restoring a backup can now only happen
        // from stable to stable and preview to preview.
        boolean isPreviousVersionStable = Util.isStableVersion(backupConfig.ybdbVersion, false);
        boolean isCurrentVersionStable = Util.isStableVersion(restoreUniverseDBVersion, false);
        // Skip version checks if runtime flag enabled. User must take care of downgrades
        if (isPreviousVersionStable ^ isCurrentVersionStable) {
          String msg =
              String.format(
                  "Cannot restore backup from preview to stable version or stable to preview. If"
                      + " required, set runtime flag 'yb.skip_version_checks' to true. Tried to"
                      + " restore a backup from '%s' to '%s'.",
                  backupConfig.ybdbVersion, restoreUniverseDBVersion);
          throw new PlatformServiceException(BAD_REQUEST, msg);
        }
        // Restore universe DB version should be greater or equal to the backup universe DB
        // current
        // version or version to which it can rollback.
        if (backupConfig.ybdbVersion != null
            && Util.compareYbVersions(
                    restoreUniverseDBVersion,
                    backupConfig.ybdbVersion,
                    true /*suppressFormatError*/)
                < 0) {
          if (backupConfig.rollbackYbdbVersion == null) {
            throw new PlatformServiceException(
                BAD_REQUEST, "Unable to restore backup as it was taken on higher DB version.");
          }
          if (backupConfig.rollbackYbdbVersion != null
              && Util.compareYbVersions(
                      restoreUniverseDBVersion,
                      backupConfig.rollbackYbdbVersion,
                      true /*suppressFormatError*/)
                  < 0) {
            throw new PlatformServiceException(
                BAD_REQUEST,
                String.format(
                    "Unable to restore as the current universe is at an older DB version %s but"
                        + " the backup was taken on a universe with DB version %s that can"
                        + " rollback only to %s",
                    restoreUniverseDBVersion,
                    backupConfig.ybdbVersion,
                    backupConfig.rollbackYbdbVersion));
          }
        }
      }
      // Validate that all master and tserver auto flags present during backup
      // should exist in restore universe.
      if (backupConfig.masterAutoFlags != null && backupConfig.tserverAutoFlags != null) {
        if (Util.compareYbVersions(
                restoreUniverseDBVersion,
                YbcBackupUtil.YBDB_AUTOFLAG_BACKUP_SUPPORT_VERSION,
                true /*suppressFormatError*/)
            < 0) {
          throw new PlatformServiceException(
              BAD_REQUEST, "Unable to restore backup as the universe does not support auto flags.");
        }
        Set<String> masterAutoFlags = backupConfig.masterAutoFlags;
        Set<String> tserverAutoFlags = backupConfig.tserverAutoFlags;
        ybcBackupUtil.validateAutoFlagCompatibility(universe, masterAutoFlags, tserverAutoFlags);
      }
    } catch (IOException e) {
      log.error("Error while validating backup metadata: ", e);
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR,
          "Error occurred while validating backup metadata " + e.getMessage());
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
