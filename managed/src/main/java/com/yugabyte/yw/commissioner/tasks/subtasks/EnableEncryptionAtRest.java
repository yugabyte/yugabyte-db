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
import com.yugabyte.yw.models.Universe;
import java.util.Arrays;
import java.util.Base64;
import java.util.List;
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
    Universe universe = Universe.getOrBadRequest(taskParams().universeUUID);
    String hostPorts = universe.getMasterAddresses();
    String certificate = universe.getCertificateNodetoNode();
    YBClient client = null;
    try {
      log.info("Running {}: hostPorts={}.", getName(), hostPorts);
      client = ybService.getClient(hostPorts, certificate);
      final byte[] universeKeyRef =
          keyManager.generateUniverseKey(
              taskParams().encryptionAtRestConfig.kmsConfigUUID,
              taskParams().universeUUID,
              taskParams().encryptionAtRestConfig);

      if (universeKeyRef == null || universeKeyRef.length == 0) {
        throw new RuntimeException("Error occurred creating universe key");
      }

      final byte[] universeKeyVal =
          keyManager.getUniverseKey(
              taskParams().universeUUID,
              taskParams().encryptionAtRestConfig.kmsConfigUUID,
              universeKeyRef,
              taskParams().encryptionAtRestConfig);

      if (universeKeyVal == null || universeKeyVal.length == 0) {
        throw new RuntimeException("Error occurred retrieving universe key from ref");
      }

      final String encodedKeyRef = Base64.getEncoder().encodeToString(universeKeyRef);

      List<HostAndPort> masterAddrs =
          Arrays.stream(hostPorts.split(","))
              .map(addr -> HostAndPort.fromString(addr))
              .collect(Collectors.toList());
      for (HostAndPort hp : masterAddrs) {
        client.addUniverseKeys(ImmutableMap.of(encodedKeyRef, universeKeyVal), hp);
      }
      for (HostAndPort hp : masterAddrs) {
        if (!client.waitForMasterHasUniverseKeyInMemory(KEY_IN_MEMORY_TIMEOUT, encodedKeyRef, hp)) {
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

      universe.incrementVersion();
      log.info("Incremented universe version to {} ", universe.version);

      EncryptionAtRestUtil.activateKeyRef(
          taskParams().universeUUID,
          taskParams().encryptionAtRestConfig.kmsConfigUUID,
          universeKeyRef);
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage(), e);
      throw new RuntimeException(e);
    } finally {
      ybService.closeClient(client, hostPorts);
    }
  }
}
