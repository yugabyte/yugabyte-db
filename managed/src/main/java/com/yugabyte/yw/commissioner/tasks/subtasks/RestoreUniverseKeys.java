/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.kms.EncryptionAtRestManager;
import com.yugabyte.yw.common.kms.EncryptionAtRestManager.RestoreKeyResult;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.models.KmsHistory;
import com.yugabyte.yw.models.Universe;
import java.util.Base64;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.YBClient;

@Slf4j
public class RestoreUniverseKeys extends AbstractTaskBase {
  // The Encryption At Rest manager
  private final EncryptionAtRestManager keyManager;

  @Inject
  protected RestoreUniverseKeys(
      BaseTaskDependencies baseTaskDependencies, EncryptionAtRestManager keyManager) {
    super(baseTaskDependencies);
    this.keyManager = keyManager;
  }

  @Override
  protected BackupTableParams taskParams() {
    return (BackupTableParams) taskParams;
  }

  // Should we use RPC to get the activeKeyId and then try and see if it matches this key?
  private byte[] getActiveUniverseKey() {
    KmsHistory activeKey = EncryptionAtRestUtil.getActiveKey(taskParams().getUniverseUUID());
    if (activeKey == null
        || activeKey.getUuid().keyRef == null
        || activeKey.getUuid().keyRef.length() == 0) {
      final String errMsg =
          String.format(
              "Skipping universe %s, No active keyRef found.",
              taskParams().getUniverseUUID().toString());
      log.trace(errMsg);
      return null;
    }

    return Base64.getDecoder().decode(activeKey.getUuid().keyRef);
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

      RestoreKeyResult restoreResult =
          keyManager.restoreUniverseKeyHistory(
              ybService,
              taskParams().getUniverseUUID(),
              taskParams().kmsConfigUUID,
              taskParams().storageLocation);

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
            keyManager.sendKeyToMasters(
                ybService,
                taskParams().getUniverseUUID(),
                universe.getUniverseDetails().encryptionAtRestConfig.kmsConfigUUID,
                activeKeyRef);
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
