/*
 * Copyright 2023 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common;

import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.provider.LocalCloudInfo;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;

@Singleton
@Slf4j
public class LocalNodeUniverseManager {

  public ShellResponse runYsqlCommand(
      NodeDetails node, Universe universe, String dbName, String ysqlCommand, long timeoutSec) {
    UniverseDefinitionTaskParams.Cluster cluster = universe.getCluster(node.placementUuid);
    LocalCloudInfo cloudInfo = LocalNodeManager.getCloudInfo(node, universe);
    List<String> bashCommand = new ArrayList<>();
    bashCommand.add(cloudInfo.getYugabyteBinDir() + "/ysqlsh");
    bashCommand.add("-h");
    bashCommand.add(node.cloudInfo.private_ip);
    bashCommand.add("-t");
    bashCommand.add("-p");
    bashCommand.add(String.valueOf(node.ysqlServerRpcPort));
    bashCommand.add("-U");
    bashCommand.add("yugabyte");
    bashCommand.add("-d");
    bashCommand.add(dbName);
    bashCommand.add("-c");
    ysqlCommand = ysqlCommand.replace("\"", "");
    bashCommand.add(ysqlCommand);

    ProcessBuilder processBuilder =
        new ProcessBuilder(bashCommand.toArray(new String[0])).redirectErrorStream(true);
    if (cluster.userIntent.enableClientToNodeEncrypt && !cluster.userIntent.enableYSQLAuth) {
      processBuilder.environment().put("sslmode", "require");
    }
    try {
      log.debug("Running command {}", String.join(" ", bashCommand));
      Process process = processBuilder.start();
      long timeOut = timeoutSec * 1000;
      while (process.isAlive() && timeOut > 0) {
        Thread.sleep(50);
        timeOut -= 50;
      }
      if (process.isAlive()) {
        throw new RuntimeException("Timed out waiting for query");
      }
      return ShellResponse.create(
          process.exitValue(),
          LocalNodeManager.COMMAND_OUTPUT_PREFIX + LocalNodeManager.getOutput(process));
    } catch (IOException | InterruptedException e) {
      throw new RuntimeException(e);
    }
  }

  public ShellResponse executeNodeAction(
      NodeUniverseManager.UniverseNodeAction nodeAction,
      List<String> commandArgs,
      ShellProcessContext context) {
    log.debug("Running Node Action {}", nodeAction.toString());
    switch (nodeAction) {
      case RUN_COMMAND:
      case RUN_SCRIPT:
      case DOWNLOAD_LOGS:
      case DOWNLOAD_FILE:
      case COPY_FILE:
      case UPLOAD_FILE:
      case TEST_DIRECTORY:
        // Todo
        break;
      case BULK_CHECK_FILES_EXIST:
        bulkCheckFilesExist(commandArgs);
        break;
      default:
        log.debug("Getting called for default");
        break;
    }

    return null;
  }

  private void bulkCheckFilesExist(List<String> commandArgs) {
    return;
  }
}
