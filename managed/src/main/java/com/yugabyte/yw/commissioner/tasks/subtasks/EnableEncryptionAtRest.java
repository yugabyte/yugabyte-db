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

import com.google.common.collect.ImmutableMap;
import com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.kms.EncryptionAtRestManager;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.KmsConfig;
import com.yugabyte.yw.models.KmsHistory;
import com.yugabyte.yw.models.Universe;
import java.util.Arrays;
import java.util.Base64;
import java.util.List;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.YBClient;
import org.yb.util.Pair;

@Slf4j
public class EnableEncryptionAtRest extends AbstractTaskBase {

  private static final int KEY_IN_MEMORY_TIMEOUT = 500;

  private final EncryptionAtRestManager keyManager;

  @Inject
  protected EnableEncryptionAtRest(
      BaseTaskDependencies baseTaskDependencies, EncryptionAtRestManager keyManager) {
    super(baseTaskDependencies);
    this.keyManager = keyManager;
  }

  public static class Params extends UniverseDefinitionTaskParams {}

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    UUID universeUUID = taskParams().getUniverseUUID();
    Universe universe = Universe.getOrBadRequest(universeUUID);
    String hostPorts = universe.getMasterAddresses();
    String certificate = universe.getCertificateNodetoNode();
    YBClient client = null;

    try {
      log.info("Running {}: hostPorts={}.", getName(), hostPorts);

      UUID kmsConfigUUID = taskParams().encryptionAtRestConfig.kmsConfigUUID;
      if (kmsConfigUUID == null) {
        throw new RuntimeException(
            "KMS config passed cannot be null when enabling encryption at rest.");
      }

      KmsHistory activeKmsHistory = EncryptionAtRestUtil.getActiveKey(universeUUID);
      int numKeys = EncryptionAtRestUtil.getNumUniverseKeys(universeUUID);

      if (numKeys > 0 && activeKmsHistory == null) {
        throw new RuntimeException(
            String.format(
                "Universe %s has %d keys but none of them are active", universeUUID, numKeys));
      }

      if (numKeys == 0 || kmsConfigUUID.equals(activeKmsHistory.getConfigUuid())) {
        // This is for both the following cases:
        // 1. Universe key creation when no universe key exists on the universe.
        // 2. Universe key rotation if the given KMS config equals the active one.
        client = ybService.getClient(hostPorts, certificate);
        byte[] universeKeyRef =
            keyManager.generateUniverseKey(
                kmsConfigUUID, universeUUID, taskParams().encryptionAtRestConfig);

        if (universeKeyRef == null || universeKeyRef.length == 0) {
          throw new RuntimeException("Error occurred creating universe key");
        }

        byte[] universeKeyVal =
            keyManager.getUniverseKey(universeUUID, kmsConfigUUID, universeKeyRef);

        if (universeKeyVal == null || universeKeyVal.length == 0) {
          throw new RuntimeException("Error occurred retrieving universe key from ref");
        }

        String encodedKeyRef = Base64.getEncoder().encodeToString(universeKeyRef);

        List<HostAndPort> masterAddrs =
            Arrays.stream(hostPorts.split(","))
                .map(addr -> HostAndPort.fromString(addr))
                .collect(Collectors.toList());
        for (HostAndPort hp : masterAddrs) {
          client.addUniverseKeys(ImmutableMap.of(encodedKeyRef, universeKeyVal), hp);
          log.info(
              "Sent universe key to universe '{}' and DB node '{}' with key ID: '{}'.",
              universeUUID,
              hp,
              encodedKeyRef);
        }
        for (HostAndPort hp : masterAddrs) {
          if (!client.waitForMasterHasUniverseKeyInMemory(
              KEY_IN_MEMORY_TIMEOUT, encodedKeyRef, hp)) {
            throw new RuntimeException(
                "Timeout occurred waiting for universe encryption key to be set in memory");
          }
        }

        client.enableEncryptionAtRestInMemory(encodedKeyRef);
        Pair<Boolean, String> isEncryptionEnabled = client.isEncryptionEnabled();
        if (!isEncryptionEnabled.getFirst()
            || !isEncryptionEnabled.getSecond().equals(encodedKeyRef)) {
          throw new RuntimeException("Error occurred enabling encryption at rest");
        }

        EncryptionAtRestUtil.activateKeyRef(universeUUID, kmsConfigUUID, universeKeyRef);
      } else if (!kmsConfigUUID.equals(activeKmsHistory.getConfigUuid())) {
        // Master key rotation case, when the given KMS config differs from the active one.
        log.info(
            String.format(
                "Rotating master key for universe '%s' from ('%s':'%s') to ('%s':'%s').",
                universeUUID,
                activeKmsHistory.getAssociatedKmsConfig().getName(),
                activeKmsHistory.getConfigUuid(),
                KmsConfig.getOrBadRequest(kmsConfigUUID).getName(),
                kmsConfigUUID));
        keyManager.reEncryptActiveUniverseKeys(universeUUID, kmsConfigUUID);
      }
      // Update the state of the universe EAR to the intended state only if above tasks run without
      // any error.
      EncryptionAtRestUtil.updateUniverseEARState(
          universeUUID, taskParams().encryptionAtRestConfig);
      universe.incrementVersion();
      log.info("Incremented universe version to {} ", universe.getVersion());
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage(), e);
      throw new RuntimeException(e);
    } finally {
      ybService.closeClient(client, hostPorts);
    }
  }
}
