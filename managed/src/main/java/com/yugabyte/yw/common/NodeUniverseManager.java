// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.common;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.NodeAgentPoller;
import com.yugabyte.yw.common.concurrent.KeyLock;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.ImageBundle;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.io.PrintWriter;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.UUID;
import java.util.concurrent.TimeUnit;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.MapUtils;
import org.apache.commons.io.FileUtils;
import org.apache.commons.lang3.StringUtils;
import play.libs.Json;

@Slf4j
@Singleton
public class NodeUniverseManager extends DevopsBase {
  private static final ShellProcessContext DEFAULT_CONTEXT =
      ShellProcessContext.builder().logCmdOutput(true).build();
  public static final String NODE_ACTION_SSH_SCRIPT = "bin/run_node_action.py";
  public static final String CERTS_DIR = "/yugabyte-tls-config";
  public static final String K8S_CERTS_DIR = "/opt/certs/yugabyte";
  public static final String NODE_UTILS_SCRIPT = "bin/node_utils.sh";

  private final KeyLock<UUID> universeLock = new KeyLock<>();

  @Inject ImageBundleUtil imageBundleUtil;
  @Inject NodeAgentPoller nodeAgentPoller;
  @Inject RuntimeConfGetter confGetter;
  @Inject LocalNodeUniverseManager localNodeUniverseManager;
  @Inject NodeActionRunner nodeActionRunner;

  @Override
  protected String getCommandType() {
    return "node";
  }

  public void downloadNodeLogs(NodeDetails node, Universe universe, String targetLocalFile) {
    universeLock.acquireLock(universe.getUniverseUUID());
    try {
      Optional<NodeAgent> optional = maybeGetNodeAgent(universe, node, true /*check feature flag*/);
      if (optional.isPresent()) {
        nodeActionRunner.downloadLogs(
            optional.get(), node, getYbHomeDir(node, universe), targetLocalFile, DEFAULT_CONTEXT);
      } else {
        List<String> actionArgs = new ArrayList<>();
        actionArgs.add("--yb_home_dir");
        actionArgs.add(getYbHomeDir(node, universe));
        actionArgs.add("--target_local_file");
        actionArgs.add(targetLocalFile);
        executeNodeAction(
                UniverseNodeAction.DOWNLOAD_LOGS, universe, node, actionArgs, DEFAULT_CONTEXT)
            .processErrors();
      }
    } finally {
      universeLock.releaseLock(universe.getUniverseUUID());
    }
  }

  public String getLocalTmpDir() {
    String localTmpDir = confGetter.getGlobalConf(GlobalConfKeys.ybTmpDirectoryPath);
    if (localTmpDir == null || localTmpDir.isEmpty()) {
      localTmpDir = "/tmp";
    }
    return localTmpDir;
  }

  public String getRemoteTmpDir(NodeDetails node, Universe universe) {
    String remoteTmpDir = GFlagsUtil.getCustomTmpDirectory(node, universe);
    if (remoteTmpDir == null || remoteTmpDir.isEmpty()) {
      remoteTmpDir = "/tmp";
    }
    return remoteTmpDir;
  }

  public String createTempFileWithSourceFiles(List<String> sourceNodeFiles) {
    String tempFilePath =
        getLocalTmpDir() + "/" + UUID.randomUUID().toString() + "-source-files.txt";
    try {
      Files.createFile(Paths.get(tempFilePath));
    } catch (IOException e) {
      log.error("Error creating file: {}", e.getMessage());
      return null;
    }

    try (PrintWriter out = new PrintWriter(new FileWriter(tempFilePath))) {
      for (String value : sourceNodeFiles) {
        out.println(value);
        log.info(value);
      }
      log.info("Above values written to file.");
    } catch (IOException e) {
      log.error("Error writing to file: {}", e.getMessage());
      return null;
    }
    return tempFilePath;
  }

  public void downloadNodeFile(
      NodeDetails node,
      Universe universe,
      String ybHomeDir,
      List<String> sourceNodeFiles,
      String targetLocalFile) {
    universeLock.acquireLock(universe.getUniverseUUID());
    String filesListFilePath = "";
    try {
      filesListFilePath = createTempFileWithSourceFiles(sourceNodeFiles);
      if (filesListFilePath == null) {
        throw new RuntimeException(
            "Could not create temp file while downloading node file for universe "
                + universe.getUniverseUUID());
      }
      Optional<NodeAgent> optional = maybeGetNodeAgent(universe, node, true /*check feature flag*/);
      if (optional.isPresent()) {
        nodeActionRunner.downloadFile(
            optional.get(), node, ybHomeDir, filesListFilePath, targetLocalFile, DEFAULT_CONTEXT);
      } else {
        List<String> actionArgs = new ArrayList<>();
        // yb_home_dir denotes a custom starting directory for the remote file. (Eg: ~/, /mnt/d0,
        // etc.)
        actionArgs.add("--yb_home_dir");
        actionArgs.add(ybHomeDir);
        actionArgs.add("--source_node_files_path");
        actionArgs.add(filesListFilePath);
        actionArgs.add("--target_local_file");
        actionArgs.add(targetLocalFile);
        executeNodeAction(
                UniverseNodeAction.DOWNLOAD_FILE, universe, node, actionArgs, DEFAULT_CONTEXT)
            .processErrors();
      }
    } finally {
      FileUtils.deleteQuietly(new File(filesListFilePath));
      universeLock.releaseLock(universe.getUniverseUUID());
    }
  }

  public void copyFileFromNode(
      NodeDetails node, Universe universe, String remoteFile, String localFile) {
    copyFileFromNode(node, universe, remoteFile, localFile, DEFAULT_CONTEXT);
  }

  public void copyFileFromNode(
      NodeDetails node,
      Universe universe,
      String remoteFile,
      String localFile,
      ShellProcessContext context) {
    Optional<NodeAgent> optional = maybeGetNodeAgent(universe, node, true /*check feature flag*/);
    if (optional.isPresent()) {
      nodeActionRunner.copyFile(optional.get(), remoteFile, localFile, DEFAULT_CONTEXT);
    } else {
      List<String> actionArgs = new ArrayList<>();
      actionArgs.add("--remote_file");
      actionArgs.add(remoteFile);
      actionArgs.add("--local_file");
      actionArgs.add(localFile);
      executeNodeAction(UniverseNodeAction.COPY_FILE, universe, node, actionArgs, context)
          .processErrors();
    }
  }

  public ShellResponse bulkCheckFilesExist(
      NodeDetails node,
      Universe universe,
      String ybDir,
      String sourceFilesPath,
      String targetLocalFilePath) {
    universeLock.acquireLock(universe.getUniverseUUID());
    try {
      List<String> actionArgs = new ArrayList<>();
      actionArgs.add("--yb_dir");
      actionArgs.add(ybDir);
      actionArgs.add("--source_files_to_check_path");
      actionArgs.add(sourceFilesPath);
      actionArgs.add("--target_local_file_path");
      actionArgs.add(targetLocalFilePath);
      return executeNodeAction(
          UniverseNodeAction.BULK_CHECK_FILES_EXIST, universe, node, actionArgs, DEFAULT_CONTEXT);
    } finally {
      universeLock.releaseLock(universe.getUniverseUUID());
    }
  }

  public void uploadFileToNode(
      NodeDetails node,
      Universe universe,
      String sourceFile,
      String targetFile,
      String permissions) {
    uploadFileToNode(node, universe, sourceFile, targetFile, permissions, DEFAULT_CONTEXT);
  }

  public void uploadFileToNode(
      NodeDetails node,
      Universe universe,
      String sourceFile,
      String targetFile,
      String permissions,
      ShellProcessContext context) {
    Optional<NodeAgent> optional = maybeGetNodeAgent(universe, node, true /*check feature flag*/);
    if (optional.isPresent()) {
      nodeActionRunner.uploadFile(optional.get(), sourceFile, targetFile, permissions, context);
    } else {
      List<String> actionArgs = new ArrayList<>();
      actionArgs.add("--source_file");
      actionArgs.add(sourceFile);
      actionArgs.add("--target_file");
      actionArgs.add(targetFile);
      actionArgs.add("--permissions");
      actionArgs.add(permissions);
      executeNodeAction(UniverseNodeAction.UPLOAD_FILE, universe, node, actionArgs, context)
          .processErrors();
    }
  }

  public ShellResponse runCommand(
      NodeDetails node, Universe universe, String command, ShellProcessContext context) {
    return runCommand(node, universe, Collections.singletonList(command), context);
  }

  public ShellResponse runCommand(NodeDetails node, Universe universe, List<String> command) {
    return runCommand(node, universe, command, DEFAULT_CONTEXT);
  }

  public ShellResponse runCommand(
      NodeDetails node, Universe universe, List<String> command, ShellProcessContext context) {
    Optional<NodeAgent> optional = maybeGetNodeAgent(universe, node, true /*check feature flag*/);
    if (optional.isPresent()) {
      return nodeActionRunner.runCommand(optional.get(), command, context);
    }
    List<String> actionArgs = new ArrayList<>();
    if (MapUtils.isNotEmpty(context.getRedactedVals())) {
      actionArgs.add("--skip_cmd_logging");
    }
    actionArgs.add("--command");
    actionArgs.addAll(command);
    return executeNodeAction(UniverseNodeAction.RUN_COMMAND, universe, node, actionArgs, context);
  }

  /**
   * Runs a script on the node to test if the given directory is writable
   *
   * @param directoryPath Full directory path ending in '/'
   * @param node Node on which to test the directory
   * @param universe Universe in which the node exists
   * @return Whether the given directory can be written to.
   */
  public boolean isDirectoryWritable(String directoryPath, NodeDetails node, Universe universe) {
    List<String> actionArgs = new ArrayList<>();
    actionArgs.add("--test_directory");
    actionArgs.add(directoryPath);
    return executeNodeAction(
            UniverseNodeAction.TEST_DIRECTORY, universe, node, actionArgs, DEFAULT_CONTEXT)
        .getMessage()
        .equals("Directory is writable");
  }

  /**
   * Runs a local script with parameters passed in a list
   *
   * @param node
   * @param universe
   * @param localScriptPath
   * @param params
   * @return the ShellResponse object
   */
  public ShellResponse runScript(
      NodeDetails node, Universe universe, String localScriptPath, List<String> params) {
    return runScript(node, universe, localScriptPath, params, DEFAULT_CONTEXT);
  }

  /**
   * Runs a local script with parameters passed in a list
   *
   * @param node
   * @param universe
   * @param localScriptPath
   * @param params
   * @param context
   * @return the ShellResponse object
   */
  public ShellResponse runScript(
      NodeDetails node,
      Universe universe,
      String localScriptPath,
      List<String> params,
      ShellProcessContext context) {
    Optional<NodeAgent> optional = maybeGetNodeAgent(universe, node, true /*check feature flag*/);
    if (optional.isPresent()) {
      return nodeActionRunner.runScript(optional.get(), localScriptPath, params, context);
    }
    List<String> actionArgs = new ArrayList<>();
    actionArgs.add("--local_script_path");
    actionArgs.add(localScriptPath);
    actionArgs.add("--params");
    actionArgs.addAll(params);
    return executeNodeAction(UniverseNodeAction.RUN_SCRIPT, universe, node, actionArgs, context);
  }

  public ShellResponse runYbAdminCommand(
      NodeDetails node,
      Universe universe,
      String ybAdminCommand,
      List<String> args,
      long timeoutSec) {
    List<String> command = new ArrayList<>();
    command.add(getYbHomeDir(node, universe) + "/master/bin/yb-admin");
    command.add("--master_addresses");
    command.add(universe.getMasterAddresses());
    UserIntent userIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
    if (userIntent.enableNodeToNodeEncrypt) {
      command.add("-certs_dir_name");
      command.add(getCertsDir(universe, node));
    }
    command.add("-timeout_ms");
    command.add(String.valueOf(TimeUnit.SECONDS.toMillis(timeoutSec)));
    command.add(ybAdminCommand);
    command.addAll(args);
    ShellProcessContext context =
        ShellProcessContext.builder().logCmdOutput(true).timeoutSecs(timeoutSec).build();
    return runCommand(node, universe, command, context);
  }

  public ShellResponse runYsqlCommand(
      NodeDetails node, Universe universe, String dbName, String ysqlCommand) {
    return runYsqlCommand(
        node,
        universe,
        dbName,
        ysqlCommand,
        confGetter.getConfForScope(universe, UniverseConfKeys.ysqlTimeoutSecs));
  }

  public ShellResponse runYsqlCommand(
      NodeDetails node, Universe universe, String dbName, String ysqlCommand, long timeoutSec) {
    boolean authEnabled =
        universe.getUniverseDetails().getPrimaryCluster().userIntent.isYSQLAuthEnabled();
    return runYsqlCommand(
        node,
        universe,
        dbName,
        ysqlCommand,
        confGetter.getConfForScope(universe, UniverseConfKeys.ysqlTimeoutSecs),
        authEnabled);
  }

  public ShellResponse runYsqlCommand(
      NodeDetails node,
      Universe universe,
      String dbName,
      String ysqlCommand,
      long timeoutSec,
      boolean authEnabled) {
    Cluster curCluster = universe.getCluster(node.placementUuid);
    if (curCluster.userIntent.providerType == CloudType.local) {
      return localNodeUniverseManager.runYsqlCommand(
          node, universe, dbName, ysqlCommand, timeoutSec, authEnabled);
    }
    List<String> command = new ArrayList<>();
    command.add("bash");
    command.add("-c");
    List<String> bashCommand = new ArrayList<>();
    Cluster cluster = universe.getUniverseDetails().getPrimaryCluster();
    String customTmpDirectory = GFlagsUtil.getCustomTmpDirectory(node, universe);
    if (cluster.userIntent.enableClientToNodeEncrypt && !cluster.userIntent.enableYSQLAuth) {
      bashCommand.add("export sslmode=\"require\";");
    }
    bashCommand.add(getYbHomeDir(node, universe) + "/tserver/bin/ysqlsh");
    bashCommand.add("-h");
    if (authEnabled) {
      bashCommand.add(
          String.format(
              "$(dirname \"$(ls -t %s/.yb.*/.s.PGSQL.* | head -1)\")", customTmpDirectory));
    } else {
      bashCommand.add(node.cloudInfo.private_ip);
    }
    bashCommand.add("-p");
    bashCommand.add(String.valueOf(node.ysqlServerRpcPort));
    bashCommand.add("-U");
    bashCommand.add("yugabyte");
    if (StringUtils.isNotEmpty(dbName)) {
      bashCommand.add("-d");
      bashCommand.add(dbName);
    }
    bashCommand.add("-c");
    // Escaping double quotes and $ at first.
    String escapedYsqlCommand = ysqlCommand.replace("\"", "\\\"");
    escapedYsqlCommand = escapedYsqlCommand.replace("$", "\\$");
    // Escaping single quotes after for non k8s deployments.
    if (!universe.getNodeDeploymentMode(node).equals(Common.CloudType.kubernetes)) {
      escapedYsqlCommand = escapedYsqlCommand.replace("'", "'\"'\"'");
    }
    bashCommand.add("\"" + escapedYsqlCommand + "\"");
    String bashCommandStr = String.join(" ", bashCommand);
    command.add(bashCommandStr);
    Map<String, String> valsToRedact = new HashMap<>();
    if (bashCommandStr.contains(Util.YSQL_PASSWORD_KEYWORD)) {
      valsToRedact.put(bashCommandStr, Util.redactYsqlQuery(bashCommandStr));
    }
    ShellProcessContext context =
        ShellProcessContext.builder()
            .logCmdOutput(valsToRedact.isEmpty())
            .timeoutSecs(timeoutSec)
            .redactedVals(valsToRedact)
            .build();
    return runCommand(node, universe, command, context);
  }

  /**
   * Returns yb home directory for node
   *
   * @param node
   * @param universe
   * @return home directory
   */
  public String getYbHomeDir(NodeDetails node, Universe universe) {
    UUID providerUUID =
        UUID.fromString(
            universe.getUniverseDetails().getClusterByUuid(node.placementUuid).userIntent.provider);
    Provider provider = Provider.getOrBadRequest(providerUUID);
    return provider.getYbHome();
  }

  /**
   * Placeholder method to get tmp directory for node
   *
   * @return tmp directory
   */
  public String getYbTmpDir() {
    return "/tmp";
  }

  private Optional<NodeAgent> maybeGetNodeAgent(
      Universe universe, NodeDetails node, boolean checkJavaClient) {
    UniverseDefinitionTaskParams.Cluster cluster =
        universe.getUniverseDetails().getClusterByUuid(node.placementUuid);
    CloudType cloudType = universe.getNodeDeploymentMode(node);
    if (cloudType == CloudType.kubernetes) {
      log.debug("Node agent is not supported on provider type {}", cloudType);
      return Optional.empty();
    }
    // Check the feature flag to either enable or disable Java client.
    if (checkJavaClient
        && !confGetter.getConfForScope(
            universe, UniverseConfKeys.nodeAgentNodeActionUseJavaClient)) {
      log.debug("Node agent is not enabled for java client");
      return Optional.empty();
    }
    UUID providerUUID = UUID.fromString(cluster.userIntent.provider);
    Provider provider = Provider.getOrBadRequest(providerUUID);
    Optional<NodeAgent> optional =
        getNodeAgentClient().maybeGetNodeAgent(node.cloudInfo.private_ip, provider);
    if (!optional.isPresent()) {
      log.debug(
          "Node agent is not enabled for provider {}({})", provider.getName(), provider.getUuid());
      return optional;
    }
    NodeAgent nodeAgent = optional.get();
    if (nodeAgentPoller.upgradeNodeAgent(nodeAgent.getUuid(), true)) {
      nodeAgent.refresh();
    }
    return optional;
  }

  private void addConnectionParams(
      Universe universe,
      NodeDetails node,
      List<String> commandArgs,
      Map<String, String> redactedVals,
      ShellProcessContext context) {
    UniverseDefinitionTaskParams.Cluster cluster =
        universe.getUniverseDetails().getClusterByUuid(node.placementUuid);
    CloudType cloudType = universe.getNodeDeploymentMode(node);
    if (cloudType == CloudType.kubernetes) {
      Map<String, String> k8sConfig =
          KubernetesUtil.getKubernetesConfigPerPod(
                  cluster.placementInfo,
                  universe.getUniverseDetails().getNodesInCluster(cluster.uuid))
              .get(node.cloudInfo.private_ip);
      if (k8sConfig == null) {
        throw new RuntimeException("Kubernetes config cannot be null");
      }
      commandArgs.add("k8s");
      commandArgs.add("--k8s_config");
      commandArgs.add(Json.stringify(Json.toJson(k8sConfig)));
    } else if (cloudType != Common.CloudType.unknown) {
      UUID providerUUID = UUID.fromString(cluster.userIntent.provider);
      Provider provider = Provider.getOrBadRequest(providerUUID);
      AccessKey accessKey =
          AccessKey.getOrBadRequest(providerUUID, cluster.userIntent.accessKeyCode);
      // No need to check the feature flag as this is not for java client.
      Optional<NodeAgent> optional =
          maybeGetNodeAgent(universe, node, false /*check feature flag*/);
      if (optional.isPresent()) {
        NodeAgent nodeAgent = optional.get();
        commandArgs.add("rpc");
        getNodeAgentClient().addNodeAgentClientParams(nodeAgent, commandArgs, redactedVals);
      } else {
        String sshPort = provider.getDetails().sshPort.toString();
        UUID imageBundleUUID =
            Util.retreiveImageBundleUUID(
                universe.getUniverseDetails().arch,
                cluster.userIntent,
                provider,
                confGetter.getStaticConf().getBoolean("yb.cloud.enabled"));
        if (imageBundleUUID != null) {
          ImageBundle.NodeProperties toOverwriteNodeProperties =
              imageBundleUtil.getNodePropertiesOrFail(
                  imageBundleUUID,
                  node.cloudInfo.region,
                  cluster.userIntent.providerType.toString());
          sshPort = toOverwriteNodeProperties.getSshPort().toString();
        }
        commandArgs.add("ssh");
        // Default SSH port can be the custom port for custom images.
        if (StringUtils.isNotBlank(context.getSshUser())
            && Util.isAddressReachable(node.cloudInfo.private_ip, 22)) {
          // In case the custom ssh User is specified in the context, that will be
          // prepare node stage, where the custom sshPort might not be configured yet.
          sshPort = "22";
        }
        commandArgs.add("--port");
        commandArgs.add(sshPort);
        commandArgs.add("--ip");
        commandArgs.add(node.cloudInfo.private_ip);
        commandArgs.add("--key");
        commandArgs.add(accessKey.getKeyInfo().privateKey);
        if (confGetter.getGlobalConf(GlobalConfKeys.ssh2Enabled)) {
          commandArgs.add("--ssh2_enabled");
        }
      }
      if (StringUtils.isNotBlank(context.getSshUser())) {
        commandArgs.add("--user");
        commandArgs.add(context.getSshUser());
      }
    }
  }

  private ShellResponse executeNodeAction(
      UniverseNodeAction nodeAction,
      Universe universe,
      NodeDetails node,
      List<String> actionArgs,
      ShellProcessContext context) {
    List<String> commandArgs = new ArrayList<>();
    Map<String, String> redactedVals = new HashMap<>();
    commandArgs.add(PY_WRAPPER);
    commandArgs.add(NODE_ACTION_SSH_SCRIPT);
    if (node.isMaster) {
      commandArgs.add("--is_master");
    }
    commandArgs.add("--node_name");
    commandArgs.add(node.nodeName);
    addConnectionParams(universe, node, commandArgs, redactedVals, context);
    commandArgs.add(nodeAction.name().toLowerCase());
    commandArgs.addAll(actionArgs);
    if (MapUtils.isNotEmpty(redactedVals)) {
      // Create a new context as a context is immutable.
      if (MapUtils.isNotEmpty(context.getRedactedVals())) {
        redactedVals.putAll(context.getRedactedVals());
      }
      context = context.toBuilder().redactedVals(redactedVals).build();
    }
    Cluster curCluster = universe.getCluster(node.placementUuid);
    if (curCluster.userIntent.providerType == CloudType.local) {
      return localNodeUniverseManager.executeNodeAction(universe, node, nodeAction, commandArgs);
    }
    return shellProcessHandler.run(commandArgs, context);
  }

  private String getCertsDir(Universe universe, NodeDetails node) {
    if (universe.getNodeDeploymentMode(node).equals(Common.CloudType.kubernetes)) {
      return K8S_CERTS_DIR;
    }
    return getYbHomeDir(node, universe) + CERTS_DIR;
  }

  /**
   * Checks if a file or directory exists on the node in the universe
   *
   * @param node
   * @param universe
   * @param remotePath
   * @return true if file/directory exists, else false
   */
  public boolean checkNodeIfFileExists(NodeDetails node, Universe universe, String remotePath) {
    List<String> params = new ArrayList<>();
    params.add("check_file_exists");
    params.add(remotePath);

    ShellResponse scriptOutput = runScript(node, universe, NODE_UTILS_SCRIPT, params);

    if (!scriptOutput.isSuccess()) {
      throw new RuntimeException(
          String.format("Failed to run command. Got error: '%s'", scriptOutput.getMessage()));
    }

    if (scriptOutput.extractRunCommandOutput().trim().equals("1")) {
      return true;
    } else {
      return false;
    }
  }

  /**
   * Try to run a simple command like ls on the remote node to see if it is responsive. If
   * unresponsive for more than `timeoutSecs`, return false.
   *
   * @param node
   * @param universe
   * @param timeoutSecs
   * @return
   */
  public boolean isNodeReachable(NodeDetails node, Universe universe, long timeoutSecs) {
    List<String> command = new ArrayList<>();
    command.add("echo");
    command.add("'test'");

    ShellProcessContext context =
        ShellProcessContext.builder().logCmdOutput(true).timeoutSecs(timeoutSecs).build();

    ShellResponse response = runCommand(node, universe, command, context);

    if (response.isSuccess()) {
      return true;
    }
    log.warn(
        "Node '{}' is unreachable for '{}' sec, or threw an error: '{}'.",
        node.getNodeName(),
        timeoutSecs,
        response.getMessage());
    return false;
  }

  /**
   * Gets a list of all the absolute file paths at a given remote directory
   *
   * @param node
   * @param universe
   * @param remoteDirPath
   * @param maxDepth
   * @param fileType
   * @return list of strings of all the absolute file paths
   */
  public List<Path> getNodeFilePaths(
      NodeDetails node, Universe universe, String remoteDirPath, int maxDepth, String fileType) {
    String localTempFilePath =
        getLocalTmpDir() + "/" + UUID.randomUUID().toString() + "-source-files-unfiltered.txt";
    String remoteTempFilePath =
        getRemoteTmpDir(node, universe)
            + "/"
            + UUID.randomUUID().toString()
            + "-source-files-unfiltered.txt";

    List<String> findCommandParams = new ArrayList<>();
    findCommandParams.add("find_paths_in_dir");
    findCommandParams.add(remoteDirPath);
    findCommandParams.add(String.valueOf(maxDepth));
    findCommandParams.add(fileType);
    findCommandParams.add(remoteTempFilePath);

    runScript(node, universe, NODE_UTILS_SCRIPT, findCommandParams).processErrors();
    // Download the files list.
    copyFileFromNode(node, universe, remoteTempFilePath, localTempFilePath);

    // Delete file from remote server after copying to local.
    List<String> removeCommand = new ArrayList<>();
    removeCommand.add("rm");
    removeCommand.add(remoteTempFilePath);
    runCommand(node, universe, removeCommand);

    // Populate the text file into array.
    List<String> nodeFilePathStrings = Arrays.asList();
    try {
      nodeFilePathStrings = Files.readAllLines(Paths.get(localTempFilePath));
    } catch (IOException e) {
      log.error("Error occurred", e);
    } finally {
      FileUtils.deleteQuietly(new File(localTempFilePath));
    }
    return nodeFilePathStrings.stream().map(Paths::get).collect(Collectors.toList());
  }

  /**
   * Returns a list of file sizes (in bytes) and their names present in a remote directory on the
   * node. This function creates a temp file with these sizes and names and copies the temp file
   * from remote to local. Then reads and processes this info from the local temp file. This is done
   * so that this operation is scalable for large number of files present on the node.
   *
   * @param node
   * @param universe
   * @param remoteDirPath
   * @return the list of pairs (size, name)
   */
  public List<Pair<Long, String>> getNodeFilePathsAndSize(
      NodeDetails node, Universe universe, String remoteDirPath) {
    String randomUUIDStr = UUID.randomUUID().toString();
    String localTempFilePath =
        getLocalTmpDir() + "/" + randomUUIDStr + "-source-files-and-sizes.txt";
    String remoteTempFilePath =
        getRemoteTmpDir(node, universe) + "/" + randomUUIDStr + "-source-files-and-sizes.txt";

    List<String> findCommandParams = new ArrayList<>();
    findCommandParams.add("get_paths_and_sizes");
    findCommandParams.add(remoteDirPath);
    findCommandParams.add(remoteTempFilePath);

    runScript(node, universe, NODE_UTILS_SCRIPT, findCommandParams).processErrors();
    // Download the files list.
    copyFileFromNode(node, universe, remoteTempFilePath, localTempFilePath);

    // Delete file from remote server after copying to local.
    List<String> removeCommand = new ArrayList<>();
    removeCommand.add("rm");
    removeCommand.add(remoteTempFilePath);
    runCommand(node, universe, removeCommand);

    // Populate the text file into array.
    List<String> nodeFilePathStrings = Arrays.asList();
    List<Pair<Long, String>> nodeFileSizePathStrings = new ArrayList<>();
    try {
      nodeFilePathStrings = Files.readAllLines(Paths.get(localTempFilePath));
      log.debug("List of files found on the node '{}': '{}'", node.nodeName, nodeFilePathStrings);
      for (String outputLine : nodeFilePathStrings) {
        String[] outputLineSplit = outputLine.split("\\s+", 2);
        if (!StringUtils.isBlank(outputLine) && outputLineSplit.length == 2) {
          nodeFileSizePathStrings.add(
              new Pair<>(Long.valueOf(outputLineSplit[0]), outputLineSplit[1]));
        }
      }
    } catch (IOException e) {
      log.error("Error occurred", e);
    } finally {
      FileUtils.deleteQuietly(new File(localTempFilePath));
    }
    return nodeFileSizePathStrings;
  }

  public enum UniverseNodeAction {
    RUN_COMMAND,
    RUN_SCRIPT,
    DOWNLOAD_LOGS,
    DOWNLOAD_FILE,
    COPY_FILE,
    UPLOAD_FILE,
    TEST_DIRECTORY,
    BULK_CHECK_FILES_EXIST
  }
}
