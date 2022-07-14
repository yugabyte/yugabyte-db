// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.RestoreUniverseKeysTaskBase;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.YbcBackupUtil;
import com.yugabyte.yw.common.YbcManager;
import com.yugabyte.yw.common.YbcBackupUtil.YbcBackupResponse;
import com.yugabyte.yw.common.kms.EncryptionAtRestManager;
import com.yugabyte.yw.common.kms.EncryptionAtRestManager.RestoreKeyResult;
import com.yugabyte.yw.forms.RestoreBackupParams;
import com.yugabyte.yw.forms.RestoreBackupParams.BackupStorageInfo;
import com.yugabyte.yw.models.Universe;
import java.util.function.Consumer;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang.StringUtils;
import org.yb.client.YBClient;
import org.yb.ybc.BackupServiceTaskCreateRequest;

@Slf4j
public class RestoreUniverseKeysYbc extends RestoreUniverseKeysTaskBase {

  private final YbcBackupUtil ybcBackupUtil;
  private final YbcManager ybcManager;

  @Inject
  protected RestoreUniverseKeysYbc(
      BaseTaskDependencies baseTaskDependencies,
      EncryptionAtRestManager keyManager,
      YbcBackupUtil ybcBackupUtil,
      YbcManager ybcManager) {
    super(baseTaskDependencies, keyManager);
    this.ybcBackupUtil = ybcBackupUtil;
    this.ybcManager = ybcManager;
  }

  @Override
  protected RestoreBackupParams taskParams() {
    return (RestoreBackupParams) taskParams;
  }

  @Override
  public void run() {
    Universe universe = Universe.getOrBadRequest(taskParams().universeUUID);
    String hostPorts = universe.getMasterAddresses();
    String certificate = universe.getCertificateNodetoNode();
    YBClient client = null;
    byte[] activeKeyRef = null;
    try {
      log.info("Running {}: hostPorts={}.", getName(), hostPorts);
      client = ybService.getClient(hostPorts, certificate);

      Consumer<JsonNode> restoreToUniverse = getUniverseKeysConsumer();

      // Retrieve the universe key set (if one is set) to restore universe to original state
      // after restoration of backup completes
      if (client.isEncryptionEnabled().getFirst()) activeKeyRef = getActiveUniverseKey();

      BackupStorageInfo backupStorageInfo = taskParams().backupStorageInfoList.get(0);
      String taskId =
          ybcBackupUtil.getYbcTaskID(
              userTaskUUID, backupStorageInfo.backupType.toString(), backupStorageInfo.keyspace);
      BackupServiceTaskCreateRequest downloadSuccessMarkerRequest =
          ybcBackupUtil.createYbcRestoreRequest(taskParams(), backupStorageInfo, taskId, true);
      String successMarkerString =
          ybcManager.downloadSuccessMarker(
              downloadSuccessMarkerRequest, taskParams().universeUUID, taskId);
      if (StringUtils.isEmpty(successMarkerString)) {
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR, "Got empty success marker response, exiting.");
      }
      YbcBackupResponse successMarker = ybcBackupUtil.parseYbcBackupResponse(successMarkerString);
      JsonNode universeKeys =
          ybcBackupUtil.getUniverseKeysJsonFromSuccessMarker(successMarker.extendedArgsString);

      RestoreKeyResult restoreResult = RestoreKeyResult.RESTORE_SKIPPED;
      if (universeKeys != null && !universeKeys.isNull()) {
        restoreResult = keyManager.restoreUniverseKeyHistory(universeKeys, restoreToUniverse);
      }

      switch (restoreResult) {
        case RESTORE_SKIPPED:
          log.info("Skipping encryption key restore...");
          break;
        case RESTORE_FAILED:
          log.info(
              String.format(
                  "Error occurred restoring encryption keys to universe %s",
                  taskParams().universeUUID));
        case RESTORE_SUCCEEDED:
          ///////////////
          // Restore state of encryption in universe having backup restored into
          ///////////////
          if (activeKeyRef != null) {
            // Ensure the active universe key in YB is set back to what it was
            // before restore flow
            sendKeyToMasters(
                activeKeyRef, universe.getUniverseDetails().encryptionAtRestConfig.kmsConfigUUID);
          } else if (client.isEncryptionEnabled().getFirst()) {
            // If there is no active keyRef but encryption is enabled,
            // it means that the universe being restored into was not
            // encrypted to begin with, and thus we should restore it back
            // to that state
            client.disableEncryptionAtRestInMemory();
            universe.incrementVersion();
          }
      }
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage(), e);
      throw new RuntimeException(e);
    } finally {
      // Close client
      if (client != null) ybService.closeClient(client, hostPorts);
    }
  }
}
