// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.common;

import com.typesafe.config.Config;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.time.Duration;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import org.apache.commons.lang3.math.NumberUtils;

/**
 * This implements most of the frequently used methods in devops/bin/run_node_action.py to directly
 * use Java node agent client instead of going through another python process.
 */
@Slf4j
@Singleton
public class NodeActionRunner {

  private final Config config;
  private final NodeAgentClient nodeAgentClient;

  @Inject
  public NodeActionRunner(Config config, NodeAgentClient nodeAgentClient) {
    this.config = config;
    this.nodeAgentClient = nodeAgentClient;
  }

  /**
   * Runs command in bash shell.
   *
   * @param nodeAgent the node agent to connect.
   * @param command the command.
   * @param context the context.
   * @return shell response.
   */
  public ShellResponse runCommand(
      NodeAgent nodeAgent, List<String> command, ShellProcessContext context) {
    List<String> shellCommand = new ArrayList<>();
    shellCommand.add("bash");
    shellCommand.add("-c");
    // Same join as in rpc.py of node agent.
    shellCommand.add(
        command.stream()
            .map(part -> part.contains(" ") ? "'" + part + "'" : part)
            .collect(Collectors.joining(" ")));
    ShellResponse response = nodeAgentClient.executeCommand(nodeAgent, shellCommand, context);
    if (response.getCode() == 0) {
      // Prefix is added to make the output same as that of run_node_action.py.
      response.message = ShellResponse.RUN_COMMAND_OUTPUT_PREFIX + response.message;
    }
    return response;
  }

  /**
   * Runs a local shell script on the remote node.
   *
   * @param nodeAgent the node agent to connect.
   * @param localScriptPath the local script path which can absolute or relative to devops home.
   * @param params the params to the script.
   * @param context the context.
   * @return shell response.
   */
  public ShellResponse runScript(
      NodeAgent nodeAgent,
      String localScriptPath,
      List<String> params,
      ShellProcessContext context) {
    Path scriptPath = Paths.get(localScriptPath);
    if (!scriptPath.isAbsolute()) {
      scriptPath = Paths.get(config.getString("yb.devops.home"), localScriptPath);
    }
    ShellResponse response = nodeAgentClient.executeScript(nodeAgent, scriptPath, params, context);
    if (response.getCode() == 0) {
      // Prefix is added to keep the output same as that of run_node_action.py.
      response.message = ShellResponse.RUN_COMMAND_OUTPUT_PREFIX + response.message;
    }
    return response;
  }

  /**
   * Downloads a support bundle tgz file to the target local file.
   *
   * @param nodeAgent the node agent to connect.
   * @param node the node in the universe.
   * @param ybHomeDir the remote home directory of yugabyte DB.
   * @param targetLocalFile the local file to which the file will be downloaded.
   * @param context the context.
   */
  public void downloadLogs(
      NodeAgent nodeAgent,
      NodeDetails node,
      String ybHomeDir,
      String targetLocalFile,
      ShellProcessContext context) {
    String user = context.getSshUserOrDefault();
    Duration timeout = context.getTimeout();
    String tarFilename = node.getNodeName() + "-support_package.tar.gz";
    List<String> command = new ArrayList<>();
    command.add("tar");
    command.add("-czvf");
    command.add(tarFilename);
    command.add("-h");
    command.add("-C");
    command.add(ybHomeDir);
    command.add("tserver/logs/yb-tserver.INFO");
    if (node.isMaster) {
      command.add("-h");
      command.add("-C");
      command.add(ybHomeDir);
      command.add("master/logs/yb-master.INFO");
    }
    // Create the tgz file.
    nodeAgentClient.executeCommand(nodeAgent, command, context).processErrors();
    try {
      nodeAgentClient.downloadFile(nodeAgent, tarFilename, targetLocalFile, user, timeout);
    } finally {
      nodeAgentClient.executeCommand(nodeAgent, Arrays.asList("rm", "-f", tarFilename), context);
    }
  }

  /**
   * Download files listed in an input file as a tgz file.
   *
   * @param nodeAgent the node agent to connect.
   * @param node the node in the universe.
   * @param ybHomeDir the remote home directory of yugabyte DB.
   * @param fileListFilepath the input list of file paths separated by new lines.
   * @param targetLocalFile the local file to which the file will be downloaded.
   * @param context the context.
   */
  public void downloadFile(
      NodeAgent nodeAgent,
      NodeDetails node,
      String ybHomeDir,
      String fileListFilepath,
      String targetLocalFile,
      ShellProcessContext context) {
    String user = context.getSshUserOrDefault();
    Duration timeout = context.getTimeout();
    String tarFilename = String.format("%s-%s.tar.gz", node.getNodeName(), UUID.randomUUID());
    String targetNodeFilesPath = fileListFilepath;
    // Upload the files to be downloaded.
    nodeAgentClient.uploadFile(nodeAgent, fileListFilepath, targetNodeFilesPath, user, 0, timeout);
    Path scriptPath =
        Paths.get(config.getString("yb.devops.home"), NodeUniverseManager.NODE_UTILS_SCRIPT);
    List<String> scriptParams = new ArrayList<>();
    scriptParams.add("create_tar_file");
    scriptParams.add(ybHomeDir);
    scriptParams.add(tarFilename);
    scriptParams.add(fileListFilepath);
    String scriptOutput =
        nodeAgentClient
            .executeScript(nodeAgent, scriptPath, scriptParams, context)
            .processErrors()
            .getMessage();
    log.debug("Output for create_tar_file for {}: {}", tarFilename, scriptOutput);

    scriptParams.clear();
    scriptParams.add("check_file_exists");
    scriptParams.add(tarFilename);
    scriptOutput =
        nodeAgentClient
            .executeScript(nodeAgent, scriptPath, scriptParams, context)
            .processErrors()
            .getMessage();
    log.debug("Output for check_file_exists for {}: {}", tarFilename, scriptOutput);

    if (scriptOutput != null && NumberUtils.toInt(scriptOutput.trim()) == 1) {
      log.info("Downloading tar file {} to {}", tarFilename, targetLocalFile);
      try {
        nodeAgentClient.downloadFile(nodeAgent, tarFilename, targetLocalFile, user, timeout);
      } finally {
        nodeAgentClient.executeCommand(
            nodeAgent, Arrays.asList("rm", "-f", tarFilename, fileListFilepath), context);
      }
    }
  }

  /**
   * Downloads a file from the remote node. It creates the local path if it does not exist.
   *
   * @param nodeAgent the node agent to connect.
   * @param remoteFile the remote file.
   * @param localFile the local file to which the file will be downloaded.
   * @param context the context.
   */
  public void copyFile(
      NodeAgent nodeAgent, String remoteFile, String localFile, ShellProcessContext context) {
    Path localFilepath = Paths.get(localFile);
    Path localFileDir = localFilepath.getParent().toAbsolutePath();
    nodeAgentClient
        .executeCommand(nodeAgent, Arrays.asList("mkdir", "-p", localFileDir.toString()), context)
        .processErrors();
    nodeAgentClient.downloadFile(nodeAgent, remoteFile, localFile);
  }

  /**
   * Uploads a file to the remote node.
   *
   * @param nodeAgent the node agent to connect.
   * @param sourceFile the local file.
   * @param targetFile the remote file.
   * @param permissions the file permission.
   * @param context the context.
   */
  public void uploadFile(
      NodeAgent nodeAgent,
      String sourceFile,
      String targetFile,
      String permissions,
      ShellProcessContext context) {
    int perm = StringUtils.isBlank(permissions) ? 0 : Integer.parseInt(permissions.trim(), 8);
    nodeAgentClient.uploadFile(
        nodeAgent,
        sourceFile,
        targetFile,
        context.getSshUserOrDefault(),
        perm,
        context.getTimeout());
  }
}
