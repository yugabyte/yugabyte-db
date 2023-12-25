// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.google.api.client.util.Throwables;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.TaskExecutor;
import com.yugabyte.yw.commissioner.YbcTaskBase;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.backuprestore.ybc.YbcBackupNodeRetriever;
import com.yugabyte.yw.common.backuprestore.ybc.YbcBackupUtil;
import com.yugabyte.yw.common.backuprestore.ybc.YbcBackupUtil.TablesMetadata;
import com.yugabyte.yw.common.backuprestore.ybc.YbcBackupUtil.YbcBackupResponse;
import com.yugabyte.yw.common.backuprestore.ybc.YbcManager;
import com.yugabyte.yw.common.services.YbcClientService;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.models.Backup;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;
import java.util.Optional;
import java.util.UUID;
import java.util.concurrent.CancellationException;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.MapUtils;
import org.apache.commons.lang3.StringUtils;
import org.yb.CommonTypes.TableType;
import org.yb.client.YbcClient;
import org.yb.ybc.BackupServiceTaskCreateRequest;
import org.yb.ybc.BackupServiceTaskCreateResponse;
import org.yb.ybc.BackupServiceTaskResultRequest;
import org.yb.ybc.BackupServiceTaskResultResponse;
import org.yb.ybc.ControllerStatus;

@Slf4j
public class BackupTableYbc extends YbcTaskBase {

  private final YbcManager ybcManager;
  private YbcClient ybcClient;
  private TaskExecutor taskExecutor;
  private String baseLogMessage = null;
  private BackupTableParams previousBackupParams = null;

  @Inject
  public BackupTableYbc(
      BaseTaskDependencies baseTaskDependencies,
      YbcClientService ybcService,
      YbcBackupUtil ybcBackupUtil,
      YbcManager ybcManager,
      TaskExecutor taskExecutor) {
    super(baseTaskDependencies, ybcService, ybcBackupUtil);
    this.ybcManager = ybcManager;
    this.taskExecutor = taskExecutor;
  }

  public static class Params extends BackupTableParams {
    public Params(BackupTableParams tableParams, YbcBackupNodeRetriever nodeRetriever) {
      super(tableParams);
      this.nodeRetriever = nodeRetriever;
    }

    // Node-ip to use as co-ordinator for the backup.
    @JsonIgnore public Backup previousBackup = null;
    public String nodeIp = null;
    public String taskID = null;
    @JsonIgnore public YbcBackupNodeRetriever nodeRetriever;
  }

  @Override
  public BackupTableYbc.Params taskParams() {
    return (BackupTableYbc.Params) taskParams;
  }

  @Override
  public void run() {
    try {
      // Check if previous backup usable
      if (taskParams().previousBackup != null) {
        Optional<BackupTableParams> oParams =
            taskParams()
                .previousBackup
                .getParamsWithIdentifier(taskParams().backupParamsIdentifier);
        previousBackupParams = oParams.isPresent() ? oParams.get() : null;
      }

      baseLogMessage =
          ybcBackupUtil.getBaseLogMessage(
              taskParams().backupUuid,
              taskParams().getKeyspace(),
              taskParams().backupParamsIdentifier);

      // Wait on node-ip
      if (StringUtils.isBlank(taskParams().nodeIp)) {
        taskParams().nodeIp = taskParams().nodeRetriever.getNodeIpForBackup();
      }

      // Ping operation is attempted again, but it's OK, since a small check only.
      ybcClient = ybcManager.getYbcClient(taskParams().getUniverseUUID(), taskParams().nodeIp);

      if (StringUtils.isBlank(taskParams().taskID)) {
        taskParams().taskID =
            ybcBackupUtil.getYbcTaskID(
                taskParams().backupUuid,
                taskParams().backupType.name(),
                taskParams().getKeyspace(),
                taskParams().backupParamsIdentifier);

        String successMarkerString = null;
        if (previousBackupParams != null) {
          String dsmTaskId =
              taskParams().taskID.concat(YbcBackupUtil.YBC_SUCCESS_MARKER_TASK_SUFFIX);
          successMarkerString =
              ybcManager.downloadSuccessMarker(
                  ybcBackupUtil.createDsmRequest(
                      taskParams().customerUuid,
                      taskParams().storageConfigUUID,
                      dsmTaskId,
                      previousBackupParams),
                  taskParams().getUniverseUUID(),
                  dsmTaskId);
          if (StringUtils.isBlank(successMarkerString)) {
            throw new RuntimeException(
                String.format(
                    "Got empty success marker for base backup with params identifier %s",
                    taskParams().backupParamsIdentifier.toString()));
          }
        }

        try {
          // For full backup, new keyspaces may have been introduced which were not in previous
          // backup, in such a case, previous backup won't have any context of it, even though
          // it's an incremental backup.
          BackupServiceTaskCreateRequest backupServiceTaskCreateRequest =
              previousBackupParams == null
                  ? ybcBackupUtil.createYbcBackupRequest(taskParams())
                  : ybcBackupUtil.createYbcBackupRequest(taskParams(), previousBackupParams);
          if (previousBackupParams != null) {
            // Fail if validation fails.
            YbcBackupResponse successMarker =
                YbcBackupUtil.parseYbcBackupResponse(successMarkerString);
            YbcBackupUtil.validateConfigWithSuccessMarker(
                successMarker, backupServiceTaskCreateRequest.getCsConfig(), true);
          }
          BackupServiceTaskCreateResponse response =
              ybcClient.backupNamespace(backupServiceTaskCreateRequest);
          if (response.getStatus().getCode().equals(ControllerStatus.OK)) {
            Backup.BackupUpdater bUpdater =
                b -> {
                  Optional<BackupTableParams> tableParamsOptional =
                      b.getParamsWithIdentifier(taskParams().backupParamsIdentifier);
                  if (tableParamsOptional.isPresent()) {
                    BackupTableParams tableParams = tableParamsOptional.get();
                    tableParams.thisBackupSubTaskStartTime = (new Date()).getTime();
                  }
                  // Update current subtask nodeIp and taskID.
                  BackupTableParams parentParams = b.getBackupInfo();
                  parentParams
                      .backupDBStates
                      .get(taskParams().backupParamsIdentifier)
                      .setIntermediate(taskParams().nodeIp, taskParams().taskID);
                };
            Backup.saveDetails(taskParams().customerUuid, taskParams().backupUuid, bUpdater);
            log.info(
                String.format(
                    "%s Successfully submitted backup task to YB-controller server: %s "
                        + "with taskID: %s",
                    baseLogMessage, taskParams().nodeIp, taskParams().taskID));
          } else if (response.getStatus().getCode().equals(ControllerStatus.EXISTS)) {
            log.info(
                String.format(
                    "%s Already present on YB-controller with taskID: %s",
                    baseLogMessage, taskParams().taskID));
          } else {
            throw new PlatformServiceException(
                response.getStatus().getCodeValue(),
                String.format(
                    "%s YB-controller returned non-zero exit status %s",
                    baseLogMessage, response.getStatus().getErrorMessage()));
          }
        } catch (Exception e) {
          log.error(
              "{} Sending backup request to YB-Controller failed with error {}",
              baseLogMessage,
              e.getMessage());
          Throwables.propagate(e);
        }
      }

      // Poll create backup progress on yb-controller and handle result
      try {
        pollTaskProgress(ybcClient, taskParams().taskID);
        handleBackupResult();
        ybcManager.deleteYbcBackupTask(
            taskParams().getUniverseUUID(), taskParams().taskID, ybcClient);
        taskParams().nodeRetriever.putNodeIPBackToPool(taskParams().nodeIp);
      } catch (Exception e) {
        log.error(
            "{} Polling backup task progress on YB-Controller failed with error {}",
            baseLogMessage,
            e.getMessage());
        Throwables.propagate(e);
      }
    } catch (CancellationException ce) {
      if (!taskExecutor.isShutdown()) {
        if (ce.getMessage().contains("Task aborted on YB-Controller")) {
          // Remove task on YB-Controller server.
          ybcManager.deleteYbcBackupTask(
              taskParams().getUniverseUUID(), taskParams().taskID, ybcClient);
        } else {
          // Explicit abort sent to YB-Controller server to abort process there.
          if (StringUtils.isNotBlank(taskParams().taskID)) {
            ybcManager.abortBackupTask(
                taskParams().customerUuid, taskParams().backupUuid, taskParams().taskID, ybcClient);
            ybcManager.deleteYbcBackupTask(
                taskParams().getUniverseUUID(), taskParams().taskID, ybcClient);
          }
        }
      }
      Throwables.propagate(new CancellationException(ce.getMessage()));
    } catch (Throwable e) {
      // Backup state will be set to Failed in main task.
      if (StringUtils.isNotBlank(taskParams().taskID)) {
        // Try abort on YB-Controller server.
        ybcManager.abortBackupTask(
            taskParams().customerUuid, taskParams().backupUuid, taskParams().taskID, ybcClient);
        ybcManager.deleteYbcBackupTask(
            taskParams().getUniverseUUID(), taskParams().taskID, ybcClient);
      }
      Throwables.propagate(e);
    } finally {
      try {
        ybcService.closeClient(ybcClient);
      } catch (Exception e) {
      }
    }
  }

  /** Update backup object with success metadata */
  private void handleBackupResult() throws PlatformServiceException {
    BackupServiceTaskResultRequest backupServiceTaskResultRequest =
        ybcBackupUtil.createYbcBackupResultRequest(taskParams().taskID);
    BackupServiceTaskResultResponse backupServiceTaskResultResponse =
        ybcClient.backupServiceTaskResult(backupServiceTaskResultRequest);
    if (backupServiceTaskResultResponse.getTaskStatus().equals(ControllerStatus.OK)) {
      YbcBackupUtil.YbcBackupResponse response =
          YbcBackupUtil.parseYbcBackupResponse(backupServiceTaskResultResponse.getMetadataJson());
      long backupSize = Long.parseLong(response.backupSize);
      Backup.BackupUpdater bUpdater =
          b -> {
            Optional<BackupTableParams> tableParamsOptional =
                b.getParamsWithIdentifier(taskParams().backupParamsIdentifier);
            if (tableParamsOptional.isPresent()) {
              BackupTableParams tableParams = tableParamsOptional.get();
              tableParams.backupSizeInBytes = backupSize;
              tableParams.timeTakenPartial = backupServiceTaskResultResponse.getTimeTakenMs();
              // Make tableNameList and tableUUIDList same as the actual snapshot content.
              if (taskParams().backupType.equals(TableType.YQL_TABLE_TYPE)) {
                TablesMetadata tablesMetadata =
                    YbcBackupUtil.getTableListFromSuccessMarker(
                        response, TableType.YQL_TABLE_TYPE, true);
                List<String> tableNameList = new ArrayList<>();
                List<UUID> tableUUIDList = new ArrayList<>();
                tablesMetadata.getTableDetailsMap().entrySet().stream()
                    .forEach(
                        tE -> {
                          tableNameList.add(tE.getKey());
                          tableUUIDList.add(tE.getValue().getTableIdentifier());
                        });
                tableParams.tableNameList = tableNameList;
                tableParams.tableUUIDList = tableUUIDList;
                tableParams.setTablesWithIndexesMap(tablesMetadata.getTablesWithIndexesMap());
              }
              // Add specific storage locations for regional backups
              if (MapUtils.isNotEmpty(response.responseCloudStoreSpec.regionLocations)) {
                tableParams.regionLocations =
                    ybcBackupUtil.extractRegionLocationFromMetadata(
                        response.responseCloudStoreSpec.regionLocations, taskParams());
              }
              BackupTableParams parentParams = b.getBackupInfo();
              parentParams
                  .backupDBStates
                  .get(taskParams().backupParamsIdentifier)
                  .resetOnComplete();
            }
          };
      Backup.saveDetails(taskParams().customerUuid, taskParams().backupUuid, bUpdater);
    } else {
      throw new PlatformServiceException(
          backupServiceTaskResultResponse.getTaskStatus().getNumber(),
          String.format(
              "%s YB-controller returned non-zero exit status %s",
              baseLogMessage, backupServiceTaskResultResponse.getTaskStatus().name()));
    }
  }
}
