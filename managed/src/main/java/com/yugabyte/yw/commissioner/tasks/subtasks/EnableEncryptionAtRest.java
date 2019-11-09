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

import java.io.File;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Universe;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.YBClient;
import play.api.Play;

public class EnableEncryptionAtRest extends AbstractTaskBase {

  public static final Logger LOG = LoggerFactory.getLogger(EnableEncryptionAtRest.class);

  // The YB client.
  public YBClientService ybService = null;

  // Timeout for failing to respond to pings.
  private static final long TIMEOUT_SERVER_WAIT_MS = 120000;

  public static class Params extends UniverseDefinitionTaskParams {}

  @Override
  protected Params taskParams() {
    return (Params)taskParams;
  }

  @Override
  public void initialize(ITaskParams params) {
    super.initialize(params);
    ybService = Play.current().injector().instanceOf(YBClientService.class);
  }

  @Override
  public void run() {
    Universe universe = Universe.get(taskParams().universeUUID);
    String hostPorts = universe.getMasterAddresses();
    String certificate = universe.getCertificate();
    YBClient client = null;
    if (taskParams().encryptionKeyFilePath != null) {
      try {
        LOG.info("Running {}: hostPorts={}.", getName(), hostPorts);
        client = ybService.getClient(hostPorts, certificate);
        File tempKeyFile = new File(taskParams().encryptionKeyFilePath);
        String nodeFilePath = "/home/yugabyte/encryption-key-dir/";
        if (taskParams().isKubernetesUniverse) {
          nodeFilePath = "/mnt/disk0/";
        }
        client.enableEncryptionAtRest(nodeFilePath + tempKeyFile.getName());
      } catch (Exception e) {
        LOG.error("{} hit error : {}", getName(), e.getMessage());
        throw new RuntimeException(e);
      } finally {
        ybService.closeClient(client, hostPorts);
      }
    }
  }
}
