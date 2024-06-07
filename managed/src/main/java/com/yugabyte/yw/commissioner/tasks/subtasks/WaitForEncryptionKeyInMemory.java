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

import com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil;
import com.yugabyte.yw.models.KmsHistory;
import com.yugabyte.yw.models.Universe;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.YBClient;

@Slf4j
public class WaitForEncryptionKeyInMemory extends NodeTaskBase {

  public static final int KEY_IN_MEMORY_TIMEOUT = 5000;

  @Inject
  protected WaitForEncryptionKeyInMemory(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends NodeTaskParams {
    public HostAndPort nodeAddress;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    if (universe != null
        && EncryptionAtRestUtil.getNumUniverseKeys(universe.getUniverseUUID()) > 0) {
      YBClient client = null;
      String hostPorts = universe.getMasterAddresses();
      String certificate = universe.getCertificateNodetoNode();
      try {
        client = ybService.getClient(hostPorts, certificate);
        KmsHistory activeKey = EncryptionAtRestUtil.getActiveKey(universe.getUniverseUUID());
        if (!client.waitForMasterHasUniverseKeyInMemory(
            KEY_IN_MEMORY_TIMEOUT, activeKey.getUuid().keyRef, taskParams().nodeAddress)) {
          throw new RuntimeException(
              "Timeout occurred waiting for universe encryption key to be set in memory");
        }
      } catch (Exception e) {
        log.error("{} hit error : {}", getName(), e.getMessage());
      } finally {
        ybService.closeClient(client, hostPorts);
      }
    }
  }
}
