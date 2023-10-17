// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.RestoreUniverseKeysTaskBase;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.backuprestore.ybc.YbcBackupUtil;
import com.yugabyte.yw.common.backuprestore.ybc.YbcBackupUtil.YbcBackupResponse;
import com.yugabyte.yw.common.backuprestore.ybc.YbcManager;
import com.yugabyte.yw.common.kms.EncryptionAtRestManager;
import com.yugabyte.yw.common.kms.EncryptionAtRestManager.RestoreKeyResult;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil;
import com.yugabyte.yw.forms.RestoreBackupParams;
import com.yugabyte.yw.forms.RestoreBackupParams.BackupStorageInfo;
import com.yugabyte.yw.models.Universe;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
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
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    String hostPorts = universe.getMasterAddresses();
    String certificate = universe.getCertificateNodetoNode();
    YBClient client = null;
    byte[] activeKeyRef = null;
    try {
      log.info("Running {}: hostPorts={}.", getName(), hostPorts);
      client = ybService.getClient(hostPorts, certificate);

      // Retrieve the universe key set (if one is set) to restore universe to original state
      // after restoration of backup completes
      if (client.isEncryptionEnabled().getFirst()) activeKeyRef = getActiveUniverseKey();

      BackupStorageInfo backupStorageInfo = taskParams().backupStorageInfoList.get(0);
      String taskId =
          ybcBackupUtil.getYbcTaskID(
              getUserTaskUUID(),
              backupStorageInfo.backupType.toString(),
              backupStorageInfo.keyspace);
      BackupServiceTaskCreateRequest downloadSuccessMarkerRequest =
          ybcBackupUtil.createDsmRequest(
              taskParams().customerUUID, taskParams().storageConfigUUID, taskId, backupStorageInfo);
      String successMarkerString =
          ybcManager.downloadSuccessMarker(
              downloadSuccessMarkerRequest, universe.getUniverseUUID(), taskId);
      if (StringUtils.isEmpty(successMarkerString)) {
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR, "Got empty success marker response, exiting.");
      }
      YbcBackupResponse successMarker = YbcBackupUtil.parseYbcBackupResponse(successMarkerString);
      JsonNode universeKeys =
          YbcBackupUtil.getUniverseKeysJsonFromSuccessMarker(successMarker.extendedArgsString);

      RestoreKeyResult restoreResult = RestoreKeyResult.RESTORE_SKIPPED;
      if (universeKeys != null && !universeKeys.isNull()) {
        restoreResult =
            keyManager.restoreUniverseKeyHistory(
                ybService,
                taskParams().getUniverseUUID(),
                taskParams().kmsConfigUUID,
                universeKeys);
      }

      switch (restoreResult) {
        case RESTORE_SKIPPED:
          log.info("Skipping encryption key restore...");
          break;
        case RESTORE_FAILED:
          log.info(
              String.format(
                  "Error occurred restoring encryption keys to universe %s",
                  taskParams().getUniverseUUID()));
        case RESTORE_SUCCEEDED:
          ///////////////
          // Restore state of encryption in universe having backup restored into
          ///////////////
          if (activeKeyRef != null) {
            // Ensure the active universe key in YB is set back to what it was
            // before restore flow
            sendKeyToMasters(
                universe.getUniverseDetails().encryptionAtRestConfig.kmsConfigUUID, activeKeyRef);
          } else if (client.isEncryptionEnabled().getFirst()) {
            // If there is no active keyRef but encryption is enabled,
            // it means that the universe being restored into was not
            // encrypted to begin with, and thus we should restore it back
            // to that state
            client.disableEncryptionAtRestInMemory();
            universe.incrementVersion();
            // Need to set KMS config UUID in the target universe since there are universe keys now.
            EncryptionAtRestUtil.updateUniverseKMSConfigIfNotExists(
                universe.getUniverseUUID(), taskParams().kmsConfigUUID);
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
