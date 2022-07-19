package com.yugabyte.yw.common;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.concurrent.KeyLock;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.UUID;
import java.util.concurrent.TimeUnit;
import lombok.extern.slf4j.Slf4j;

@Singleton
@Slf4j
public class NodeUniverseManager extends DevopsBase {
  private static final ShellProcessContext DEFAULT_CONTEXT =
      ShellProcessContext.builder().logCmdOutput(true).build();
  public static final long YSQL_COMMAND_DEFAULT_TIMEOUT_SEC = TimeUnit.MINUTES.toSeconds(3);
  public static final String NODE_ACTION_SSH_SCRIPT = "bin/run_node_action.py";
  public static final String CERTS_DIR = "/yugabyte-tls-config";
  public static final String K8S_CERTS_DIR = "/opt/certs/yugabyte";

  private final KeyLock<UUID> universeLock = new KeyLock<>();

  @Override
  protected String getCommandType() {
    return null;
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
      NodeDetails node, Universe universe, String ybAdminCommand, long timeoutSec) {
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
      bashCommand.add("$(dirname \"$(ls /tmp/.yb.*/.s.PGSQL.* | head -1)\")");
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
    // Escaping single quotes after.
    escapedYsqlCommand = escapedYsqlCommand.replace("'", "'\"'\"'");
    bashCommand.add("\"" + escapedYsqlCommand + "\"");
    command.add(String.join(" ", bashCommand));
    ShellProcessContext context =
        ShellProcessContext.builder().logCmdOutput(true).timeoutSecs(timeoutSec).build();
    return runCommand(node, universe, command, context);
  }

  /** returns (location of) access key for a particular node in a universe */
  private String getAccessKey(NodeDetails node, Universe universe) {
    if (node == null) {
      throw new RuntimeException("node must be nonnull");
    }
    UniverseDefinitionTaskParams.Cluster cluster =
        universe.getUniverseDetails().getClusterByUuid(node.placementUuid);
    UUID providerUUID = UUID.fromString(cluster.userIntent.provider);
    AccessKey ak = AccessKey.get(providerUUID, cluster.userIntent.accessKeyCode);
    return ak.getKeyInfo().privateKey;
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
    Provider provider = Provider.get(providerUUID);
    return provider.getYbHome();
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
    UniverseDefinitionTaskParams.Cluster cluster =
        universe.getUniverseDetails().getClusterByUuid(node.placementUuid);
    UUID providerUUID = UUID.fromString(cluster.userIntent.provider);
    if (node.isMaster) {
      commandArgs.add("--is_master");
    }
    commandArgs.add("--node_name");
    commandArgs.add(node.nodeName);
    if (universe.getNodeDeploymentMode(node).equals(Common.CloudType.kubernetes)) {
      String kubeconfig =
          PlacementInfoUtil.getKubernetesConfigPerPod(
                  cluster.placementInfo,
                  universe.getUniverseDetails().getNodesInCluster(cluster.uuid))
              .get(node.cloudInfo.private_ip);
      if (kubeconfig == null) {
        throw new RuntimeException("kubeconfig cannot be null");
      }

      commandArgs.add("k8s");
      commandArgs.add("--pod_fqdn");
      commandArgs.add(node.cloudInfo.private_ip);
      commandArgs.add("--kubeconfig");
      commandArgs.add(kubeconfig);
    } else if (!universe.getNodeDeploymentMode(node).equals(Common.CloudType.unknown)) {
      AccessKey accessKey =
          AccessKey.getOrBadRequest(providerUUID, cluster.userIntent.accessKeyCode);
      commandArgs.add("ssh");
      commandArgs.add("--port");
      commandArgs.add(accessKey.getKeyInfo().sshPort.toString());
      commandArgs.add("--ip");
      commandArgs.add(node.cloudInfo.private_ip);
      commandArgs.add("--key");
      commandArgs.add(getAccessKey(node, universe));
      if (runtimeConfigFactory.globalRuntimeConf().getBoolean("yb.security.ssh2_enabled")) {
        commandArgs.add("--ssh2_enabled");
      }
    } else {
      throw new RuntimeException("Cloud type unknown");
    }
    commandArgs.add(nodeAction.name().toLowerCase());
    commandArgs.addAll(actionArgs);
    String logMsg = "Executing command: " + commandArgs;
    if (context.isTraceLogging()) {
      log.trace(logMsg);
    } else {
      log.debug(logMsg);
    }
    return shellProcessHandler.run(commandArgs, context);
  }

  private String getCertsDir(Universe universe, NodeDetails node) {
    if (universe.getNodeDeploymentMode(node).equals(Common.CloudType.kubernetes)) {
      return K8S_CERTS_DIR;
    }
    return getYbHomeDir(node, universe) + CERTS_DIR;
  }

  public enum UniverseNodeAction {
    RUN_COMMAND,
    RUN_SCRIPT,
    DOWNLOAD_LOGS,
    DOWNLOAD_FILE,
    UPLOAD_FILE
  }
}
