package com.yugabyte.yw.common;

import com.google.inject.Singleton;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.concurrent.KeyLock;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.ProviderDetails;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
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
import org.apache.commons.collections.MapUtils;
import org.apache.commons.lang3.StringUtils;
import play.libs.Json;

@Singleton
public class NodeUniverseManager extends DevopsBase {
  private static final ShellProcessContext DEFAULT_CONTEXT =
      ShellProcessContext.builder().logCmdOutput(true).build();
  public static final long YSQL_COMMAND_DEFAULT_TIMEOUT_SEC = TimeUnit.MINUTES.toSeconds(3);
  public static final String NODE_ACTION_SSH_SCRIPT = "bin/run_node_action.py";
  public static final String CERTS_DIR = "/yugabyte-tls-config";
  public static final String K8S_CERTS_DIR = "/opt/certs/yugabyte";
  public static final String NODE_UTILS_SCRIPT = "bin/node_utils.sh";

  private final KeyLock<UUID> universeLock = new KeyLock<>();

  @Override
  protected String getCommandType() {
    return "node";
  }

  public ShellResponse downloadNodeLogs(
      NodeDetails node, Universe universe, String targetLocalFile) {
    universeLock.acquireLock(universe.getUniverseUUID());
    try {
      List<String> actionArgs = new ArrayList<>();
      actionArgs.add("--yb_home_dir");
      actionArgs.add(getYbHomeDir(node, universe));
      actionArgs.add("--target_local_file");
      actionArgs.add(targetLocalFile);
      return executeNodeAction(
          UniverseNodeAction.DOWNLOAD_LOGS, universe, node, actionArgs, DEFAULT_CONTEXT);
    } finally {
      universeLock.releaseLock(universe.getUniverseUUID());
    }
  }

  public ShellResponse downloadNodeFile(
      NodeDetails node,
      Universe universe,
      String ybHomeDir,
      String sourceNodeFile,
      String targetLocalFile) {
    universeLock.acquireLock(universe.getUniverseUUID());
    try {
      List<String> actionArgs = new ArrayList<>();
      // yb_home_dir denotes a custom starting directory for the remote file. (Eg: ~/, /mnt/d0,
      // etc.)
      actionArgs.add("--yb_home_dir");
      actionArgs.add(ybHomeDir);
      actionArgs.add("--source_node_files");
      actionArgs.add(sourceNodeFile);
      actionArgs.add("--target_local_file");
      actionArgs.add(targetLocalFile);
      return executeNodeAction(
          UniverseNodeAction.DOWNLOAD_FILE, universe, node, actionArgs, DEFAULT_CONTEXT);
    } finally {
      universeLock.releaseLock(universe.getUniverseUUID());
    }
  }

  public ShellResponse uploadFileToNode(
      NodeDetails node,
      Universe universe,
      String sourceFile,
      String targetFile,
      String permissions) {
    return uploadFileToNode(node, universe, sourceFile, targetFile, permissions, DEFAULT_CONTEXT);
  }

  public ShellResponse uploadFileToNode(
      NodeDetails node,
      Universe universe,
      String sourceFile,
      String targetFile,
      String permissions,
      ShellProcessContext context) {
    List<String> actionArgs = new ArrayList<>();
    actionArgs.add("--source_file");
    actionArgs.add(sourceFile);
    actionArgs.add("--target_file");
    actionArgs.add(targetFile);
    actionArgs.add("--permissions");
    actionArgs.add(permissions);
    return executeNodeAction(UniverseNodeAction.UPLOAD_FILE, universe, node, actionArgs, context);
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
    List<String> actionArgs = new ArrayList<>();
    if (MapUtils.isNotEmpty(context.getRedactedVals())) {
      actionArgs.add("--skip_cmd_logging");
    }
    actionArgs.add("--command");
    actionArgs.addAll(command);
    return executeNodeAction(UniverseNodeAction.RUN_COMMAND, universe, node, actionArgs, context);
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
    return runYsqlCommand(node, universe, dbName, ysqlCommand, YSQL_COMMAND_DEFAULT_TIMEOUT_SEC);
  }

  public ShellResponse runYsqlCommand(
      NodeDetails node, Universe universe, String dbName, String ysqlCommand, long timeoutSec) {
    List<String> command = new ArrayList<>();
    command.add("bash");
    command.add("-c");
    List<String> bashCommand = new ArrayList<>();
    Cluster cluster = universe.getUniverseDetails().getPrimaryCluster();
    if (cluster.userIntent.enableClientToNodeEncrypt && !cluster.userIntent.enableYSQLAuth) {
      bashCommand.add("export sslmode=\"require\";");
    }
    bashCommand.add(getYbHomeDir(node, universe) + "/tserver/bin/ysqlsh");
    bashCommand.add("-h");
    if (cluster.userIntent.isYSQLAuthEnabled()) {
      bashCommand.add("$(dirname \"$(ls -t /tmp/.yb.*/.s.PGSQL.* | head -1)\")");
    } else {
      bashCommand.add(node.cloudInfo.private_ip);
    }
    bashCommand.add("-p");
    bashCommand.add(String.valueOf(node.ysqlServerRpcPort));
    bashCommand.add("-U");
    bashCommand.add("yugabyte");
    bashCommand.add("-d");
    bashCommand.add(dbName);
    bashCommand.add("-c");
    // Escaping double quotes at first.
    String escapedYsqlCommand = ysqlCommand.replace("\"", "\\\"");
    // Escaping single quotes after for non k8s deployments.
    if (!universe.getNodeDeploymentMode(node).equals(Common.CloudType.kubernetes)) {
      escapedYsqlCommand = escapedYsqlCommand.replace("$", "\\$");
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

  private void addConnectionParams(
      Universe universe, NodeDetails node, ShellProcessContext context, List<String> commandArgs) {
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
      ProviderDetails providerDetails = Provider.getOrBadRequest(providerUUID).details;
      AccessKey accessKey =
          AccessKey.getOrBadRequest(providerUUID, cluster.userIntent.accessKeyCode);
      Optional<NodeAgent> optional =
          getNodeAgentClient().maybeGetNodeAgentClient(node.cloudInfo.private_ip);
      if (optional.isPresent()) {
        commandArgs.add("rpc");
        NodeAgentClient.addNodeAgentClientParams(optional.get(), commandArgs);
      } else {
        commandArgs.add("ssh");
        commandArgs.add("--port");
        commandArgs.add(providerDetails.sshPort.toString());
        commandArgs.add("--ip");
        commandArgs.add(node.cloudInfo.private_ip);
        commandArgs.add("--key");
        commandArgs.add(accessKey.getKeyInfo().privateKey);
        if (runtimeConfigFactory.globalRuntimeConf().getBoolean("yb.security.ssh2_enabled")) {
          commandArgs.add("--ssh2_enabled");
        }
      }
      if (context.isCustomUser() && StringUtils.isNotBlank(providerDetails.sshUser)) {
        commandArgs.add("--user");
        commandArgs.add(providerDetails.sshUser);
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

    commandArgs.add(PY_WRAPPER);
    commandArgs.add(NODE_ACTION_SSH_SCRIPT);
    if (node.isMaster) {
      commandArgs.add("--is_master");
    }
    commandArgs.add("--node_name");
    commandArgs.add(node.nodeName);
    addConnectionParams(universe, node, context, commandArgs);
    commandArgs.add(nodeAction.name().toLowerCase());
    commandArgs.addAll(actionArgs);
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

    if (scriptOutput.extractRunCommandOutput().trim().equals("1")) {
      return true;
    } else {
      return false;
    }
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
    List<String> command = new ArrayList<>();
    command.add("find");
    command.add(remoteDirPath);
    command.add("-maxdepth");
    command.add(String.valueOf(maxDepth));
    command.add("-type");
    command.add(fileType);

    ShellResponse shellOutput = runCommand(node, universe, command);
    List<String> nodeFilePathStrings =
        Arrays.asList(shellOutput.extractRunCommandOutput().trim().split("\n", 0));
    return nodeFilePathStrings.stream().map(Paths::get).collect(Collectors.toList());
  }

  public enum UniverseNodeAction {
    RUN_COMMAND,
    RUN_SCRIPT,
    DOWNLOAD_LOGS,
    DOWNLOAD_FILE,
    UPLOAD_FILE
  }
}
