package com.yugabyte.yw.commissioner;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableMap;
import com.google.common.net.HostAndPort;
import com.yugabyte.yw.common.kms.EncryptionAtRestManager;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil;
import com.yugabyte.yw.forms.RestoreBackupParams;
import com.yugabyte.yw.models.KmsHistory;
import com.yugabyte.yw.models.Universe;
import java.util.Arrays;
import java.util.Base64;
import java.util.List;
import java.util.UUID;
import java.util.function.Consumer;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.YBClient;
import org.yb.util.Pair;

@Slf4j
public abstract class RestoreUniverseKeysTaskBase extends AbstractTaskBase {

  // How long to wait for universe key to be set in memory
  protected static final int KEY_IN_MEMORY_TIMEOUT = 500;

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
    KmsHistory activeKey = EncryptionAtRestUtil.getActiveKey(taskParams().universeUUID);
    if (activeKey == null || activeKey.uuid.keyRef == null || activeKey.uuid.keyRef.length() == 0) {
      final String errMsg =
          String.format(
              "Skipping universe %s, No active keyRef found.",
              taskParams().universeUUID.toString());
      log.trace(errMsg);
      return null;
    }

    return Base64.getDecoder().decode(activeKey.uuid.keyRef);
  }

  protected Consumer<JsonNode> getUniverseKeysConsumer() {
    return (JsonNode backupEntry) -> {
      final byte[] universeKeyRef = Base64.getDecoder().decode(backupEntry.get("key_ref").asText());

      if (universeKeyRef != null) {
        // Restore keys to database
        keyManager
            .getServiceInstance(backupEntry.get("key_provider").asText())
            .restoreBackupEntry(
                taskParams().universeUUID, taskParams().kmsConfigUUID, universeKeyRef);
        sendKeyToMasters(universeKeyRef, taskParams().kmsConfigUUID);
      }
    };
  }

  protected void sendKeyToMasters(byte[] keyRef, UUID kmsConfigUUID) {
    Universe universe = Universe.getOrBadRequest(taskParams().universeUUID);
    String hostPorts = universe.getMasterAddresses();
    String certificate = universe.getCertificateNodetoNode();
    YBClient client = null;
    try {
      byte[] keyVal = keyManager.getUniverseKey(taskParams().universeUUID, kmsConfigUUID, keyRef);
      String encodedKeyRef = Base64.getEncoder().encodeToString(keyRef);
      client = ybService.getClient(hostPorts, certificate);
      List<HostAndPort> masterAddrs =
          Arrays.stream(hostPorts.split(","))
              .map(addr -> HostAndPort.fromString(addr))
              .collect(Collectors.toList());
      for (HostAndPort hp : masterAddrs) {
        client.addUniverseKeys(ImmutableMap.of(encodedKeyRef, keyVal), hp);
      }
      for (HostAndPort hp : masterAddrs) {
        if (!client.waitForMasterHasUniverseKeyInMemory(KEY_IN_MEMORY_TIMEOUT, encodedKeyRef, hp)) {
          throw new RuntimeException(
              "Timeout occurred waiting for universe encryption key to be " + "set in memory");
        }
      }

      // Since a universe key only gets written to the universe key registry during a
      // change encryption info request, we need to temporarily enable encryption with each
      // key to ensure it is written to the registry to be used to decrypt restored files
      client.enableEncryptionAtRestInMemory(encodedKeyRef);
      Pair<Boolean, String> isEncryptionEnabled = client.isEncryptionEnabled();
      if (!isEncryptionEnabled.getFirst()
          || !isEncryptionEnabled.getSecond().equals(encodedKeyRef)) {
        throw new RuntimeException("Master did not respond that key was enabled");
      }

      universe.incrementVersion();

      // Activate keyRef so that if the universe is not enabled,
      // the last keyRef will always be in-memory due to the setkey task
      // which will mean the cluster will always be able to decrypt the
      // universe key registry which we need to be the case.
      EncryptionAtRestUtil.activateKeyRef(taskParams().universeUUID, kmsConfigUUID, keyRef);
    } catch (Exception e) {
      log.error("Error sending universe key to master: ", e);
    } finally {
      ybService.closeClient(client, hostPorts);
    }
  }
}
