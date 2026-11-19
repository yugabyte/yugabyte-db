// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.google.common.base.Throwables;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.kms.EncryptionAtRestManager;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.KmsConfig;
import com.yugabyte.yw.models.KmsHistory;
import java.util.Base64;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

/**
 * Precheck that fails the task if YBA cannot get the active universe key from KMS. It skips the key
 * cache, so a lost KMS access shows up before any master stops without the key.
 */
@Slf4j
public class CheckUniverseKeyInKms extends AbstractTaskBase {

  private final EncryptionAtRestManager keyManager;

  public static class Params extends UniverseTaskParams {}

  @Inject
  protected CheckUniverseKeyInKms(
      BaseTaskDependencies baseTaskDependencies, EncryptionAtRestManager keyManager) {
    super(baseTaskDependencies);
    this.keyManager = keyManager;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    UUID universeUUID = taskParams().getUniverseUUID();
    KmsHistory activeKey = EncryptionAtRestUtil.getActiveKey(universeUUID);
    if (activeKey == null
        || activeKey.getUuid().keyRef == null
        || activeKey.getUuid().keyRef.isEmpty()) {
      log.info("No active universe key for universe {}, skipping KMS check", universeUUID);
      return;
    }
    KmsConfig kmsConfig = KmsConfig.get(activeKey.getConfigUuid());
    if (kmsConfig == null) {
      throw new RuntimeException(
          String.format(
              "YBA cannot get the active universe key: KMS config %s is missing.",
              activeKey.getConfigUuid()));
    }
    byte[] keyRef = Base64.getDecoder().decode(activeKey.getUuid().keyRef);
    String cause = "KMS returned no key, see the YBA log";
    Exception kmsError = null;
    byte[] keyVal = null;
    try {
      keyVal =
          keyManager
              .getServiceInstance(kmsConfig.getKeyProvider().name())
              .retrieveKeyFromKms(
                  universeUUID,
                  kmsConfig.getConfigUUID(),
                  keyRef,
                  activeKey.getEncryptionContext());
    } catch (Exception e) {
      log.error("Failed to get universe key from KMS for universe {}", universeUUID, e);
      kmsError = e;
      Throwable rootCause = Throwables.getRootCause(e);
      cause =
          rootCause.getMessage() != null
              ? rootCause.getMessage()
              : rootCause.getClass().getSimpleName();
    }
    if (keyVal == null) {
      throw new RuntimeException(
          String.format(
              "YBA cannot get the active universe key from KMS config '%s': %s. Restore KMS"
                  + " access before running this task, or restarted masters will not get the key.",
              kmsConfig.getName(), cause),
          kmsError);
    }
    log.info(
        "Got the active universe key from KMS config '{}' for universe {}",
        kmsConfig.getName(),
        universeUUID);
  }
}
