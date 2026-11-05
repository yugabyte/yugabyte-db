// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.common.Util.UniverseDetailSubset;
import com.yugabyte.yw.common.kms.EncryptionAtRestManager;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil;
import com.yugabyte.yw.models.KmsConfig;
import com.yugabyte.yw.models.KmsHistory;
import com.yugabyte.yw.models.Universe;
import java.util.Base64;
import java.util.List;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Abortable
@Retryable
public class EditKMSConfig extends KMSConfigTaskBase {

  @Inject
  protected EditKMSConfig(
      BaseTaskDependencies baseTaskDependencies, EncryptionAtRestManager kmsManager) {
    super(baseTaskDependencies, kmsManager);
  }

  @Override
  public void run() {
    log.info("Editing KMS Configuration for customer: " + taskParams().customerUUID.toString());

    // Validating the new authConfig by generating new keyValue for associated universes.
    // We will not be storing the new keyValue, the universes are going to use their old keyRef,
    // keyValue.
    try {
      List<UUID> universeUUIDList =
          EncryptionAtRestUtil.getUniverses(taskParams().configUUID).stream()
              .map(UniverseDetailSubset::getUuid)
              .collect(Collectors.toList());
      for (UUID universeUUID : universeUUIDList) {
        Universe universe = Universe.maybeGet(universeUUID).orElse(null);
        if (universe != null) {
          // Validate the active key when this config owns it. After a master key rotation to
          // another config, the active key belongs to that config and this provider cannot
          // decrypt it, so fall back to the latest key this config encrypted.
          KmsHistory configKey = EncryptionAtRestUtil.getActiveKey(universeUUID);
          if (configKey == null) {
            log.info(
                "Skipping retrieving keys validation for universe "
                    + universeUUID
                    + " as no active key was found");
            continue;
          }
          if (!taskParams().configUUID.equals(configKey.getConfigUuid())) {
            configKey =
                EncryptionAtRestUtil.getLatestConfigKey(universeUUID, taskParams().configUUID);
            if (configKey == null) {
              log.info(
                  "Skipping retrieving keys validation for universe "
                      + universeUUID
                      + " as no key was found for this KMS config");
              continue;
            }
          }
          byte[] keyRef = Base64.getDecoder().decode(configKey.getUuid().keyRef);
          if (kmsManager
                  .getServiceInstance(taskParams().kmsProvider.name())
                  .validateConfigForUpdate(
                      universeUUID,
                      taskParams().configUUID,
                      keyRef,
                      configKey.encryptionContext,
                      universe.getUniverseDetails().encryptionAtRestConfig,
                      taskParams().providerConfig)
              == null) {
            throw new RuntimeException("Error editing the kms config");
          }
        }
      }
    } catch (Exception e) {
      log.info("Editing KMS task failed due to " + e.getMessage());
      throw new RuntimeException(e.getMessage());
    }

    KmsConfig updateResult =
        kmsManager
            .getServiceInstance(taskParams().kmsProvider.name())
            .updateAuthConfig(taskParams().configUUID, taskParams().providerConfig);

    if (updateResult == null) {
      throw new RuntimeException(
          String.format(
              "Error updating KMS Configuration for customer %s and kms provider %s",
              taskParams().customerUUID.toString(), taskParams().kmsProvider));
    }
  }
}
