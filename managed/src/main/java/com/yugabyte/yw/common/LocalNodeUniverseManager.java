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

import static com.yugabyte.yw.common.ShellResponse.ERROR_CODE_SUCCESS;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.provider.LocalCloudInfo;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.nio.file.StandardCopyOption;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;

@Singleton
@Slf4j
public class LocalNodeUniverseManager {
  @Inject LocalNodeManager localNodeManager;

  public ShellResponse runYsqlCommand(
      NodeDetails node, Universe universe, String dbName, String ysqlCommand, long timeoutSec) {
    return runYsqlCommand(node, universe, dbName, ysqlCommand, timeoutSec, false);
  }

  public ShellResponse runYsqlCommand(
      NodeDetails node,
      Universe universe,
      String dbName,
      String ysqlCommand,
      long timeoutSec,
      boolean authEnabled) {
    UniverseDefinitionTaskParams.Cluster cluster = universe.getCluster(node.placementUuid);
    LocalCloudInfo cloudInfo = LocalNodeManager.getCloudInfo(node, universe);
    List<String> bashCommand = new ArrayList<>();
    bashCommand.add(cloudInfo.getYugabyteBinDir() + "/ysqlsh");
    bashCommand.add("-h");
    if (authEnabled) {
      String customTmpDirectory = getTmpDir(node, universe);
      log.debug("customTmpDirectory {}", customTmpDirectory);
      bashCommand.add(
          String.format(
              "%s/.yb.%s:%s",
              customTmpDirectory, node.cloudInfo.private_ip, node.ysqlServerRpcPort));
    } else {
      bashCommand.add(node.cloudInfo.private_ip);
    }
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
      Universe universe,
      NodeDetails node,
      NodeUniverseManager.UniverseNodeAction nodeAction,
      List<String> commandArgs) {
    log.debug("Running Node Action {}", nodeAction.toString());
    switch (nodeAction) {
      case RUN_COMMAND:
        return runCommand(universe, node, commandArgs);
      case UPLOAD_FILE:
        return uploadFile(universe, node, commandArgs);
      case RUN_SCRIPT:
      case DOWNLOAD_LOGS:
      case DOWNLOAD_FILE:
      case COPY_FILE:
      case TEST_DIRECTORY:
      case BULK_CHECK_FILES_EXIST:
        // Todo
      default:
    }

    return ShellResponse.create(ERROR_CODE_SUCCESS, "Lost!");
  }

  private ShellResponse runCommand(Universe universe, NodeDetails node, List<String> commandArgs) {
    try {
      UniverseDefinitionTaskParams.UserIntent userIntent = getUserIntent(universe, node);
      int commandIndex = commandArgs.indexOf("--command");
      List<String> commandArguments = commandArgs.subList(commandIndex + 1, commandArgs.size());

      for (int i = 0; i < commandArguments.size(); i++) {
        if (commandArguments.get(i).startsWith(CommonUtils.DEFAULT_YB_HOME_DIR)) {
          commandArguments.set(
              i,
              commandArguments
                  .get(i)
                  .replace(
                      CommonUtils.DEFAULT_YB_HOME_DIR,
                      localNodeManager.getNodeRoot(userIntent, node.nodeName)));
        }
      }
      runProcess(commandArguments, null);
    } catch (IOException | InterruptedException e) {
      throw new RuntimeException(e);
    }
    return ShellResponse.create(ERROR_CODE_SUCCESS, "Success!");
  }

  private int runProcess(List<String> commandArguments, Map<String, String> envVars)
      throws IOException, InterruptedException {
    ProcessBuilder processBuilder =
        new ProcessBuilder(commandArguments.toArray(new String[0])).redirectErrorStream(true);
    if (envVars != null) {
      processBuilder.environment().putAll(envVars);
    }
    log.debug("Running command {}", String.join(" ", commandArguments));
    Process process = processBuilder.start();
    int exitCode = process.waitFor();
    return exitCode;
  }

  private ShellResponse uploadFile(Universe universe, NodeDetails node, List<String> commandArgs) {
    try {
      UniverseDefinitionTaskParams.UserIntent userIntent = getUserIntent(universe, node);
      Map<String, String> args = LocalNodeManager.convertCommandArgListToMap(commandArgs);
      String targetFilePath =
          args.get("--target_file")
              .replace(
                  CommonUtils.DEFAULT_YB_HOME_DIR,
                  localNodeManager.getNodeRoot(userIntent, node.nodeName));
      Files.copy(
          Paths.get(args.get("--source_file")),
          Paths.get(targetFilePath),
          StandardCopyOption.REPLACE_EXISTING);
      localNodeManager.setFilePermissions(targetFilePath);
    } catch (IOException e) {
      throw new RuntimeException(e);
    }

    return ShellResponse.create(ERROR_CODE_SUCCESS, "Success!");
  }

  private UniverseDefinitionTaskParams.UserIntent getUserIntent(
      Universe universe, NodeDetails node) {
    return universe.getUniverseDetails().getClusterByUuid(node.placementUuid).userIntent;
  }

  private String getTmpDir(NodeDetails node, Universe universe) {
    UniverseDefinitionTaskParams.Cluster cluster =
        universe.getUniverseDetails().getClusterByUuid(node.placementUuid);
    Map<String, String> gflags =
        GFlagsUtil.getGFlagsForNode(
            node,
            UniverseTaskBase.ServerType.TSERVER,
            cluster,
            universe.getUniverseDetails().clusters);
    if (gflags.containsKey(GFlagsUtil.TMP_DIRECTORY)) {
      return localNodeManager.getTmpDir(gflags, node.getNodeName(), cluster.userIntent);
    }
    return GFlagsUtil.getCustomTmpDirectory(node, universe);
  }
}
