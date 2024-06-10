package com.yugabyte.yw.commissioner;

import com.yugabyte.yw.common.kms.EncryptionAtRestManager;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil;
import com.yugabyte.yw.forms.RestoreBackupParams;
import com.yugabyte.yw.models.KmsHistory;
import java.util.Base64;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public abstract class RestoreUniverseKeysTaskBase extends AbstractTaskBase {
  // The Encryption At Rest manager
  protected final EncryptionAtRestManager keyManager;

  @Inject
  public RestoreUniverseKeysTaskBase(
      BaseTaskDependencies baseTaskDependencies, EncryptionAtRestManager keyManager) {
    super(baseTaskDependencies);
    this.keyManager = keyManager;
  }

  @Override
  protected RestoreBackupParams taskParams() {
    return (RestoreBackupParams) taskParams;
  }

  // Should we use RPC to get the activeKeyId and then try and see if it matches this key?
  protected byte[] getActiveUniverseKey() {
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

  protected void sendKeyToMasters(UUID kmsConfigUUID, byte[] keyRef) {
    keyManager.sendKeyToMasters(ybService, taskParams().getUniverseUUID(), kmsConfigUUID, keyRef);
  }
}
