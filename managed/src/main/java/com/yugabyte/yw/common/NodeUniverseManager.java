package com.yugabyte.yw.common;

import com.google.inject.Singleton;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.UUID;

@Singleton
public class NodeUniverseManager extends DevopsBase {
  public static final int YSQL_COMMAND_DEFAULT_TIMEOUT_SEC = 20;
  public static final String NODE_ACTION_SSH_SCRIPT = "bin/run_node_action.py";

  @Override
  protected String getCommandType() {
    return null;
  }

  public synchronized ShellResponse downloadNodeLogs(
      NodeDetails node, Universe universe, String targetLocalFile) {
    List<String> actionArgs = new ArrayList<>();
    actionArgs.add("--yb_home_dir");
    actionArgs.add(getYbHomeDir(node, universe));
    actionArgs.add("--target_local_file");
    actionArgs.add(targetLocalFile);
    return executeNodeAction(UniverseNodeAction.DOWNLOAD_LOGS, universe, node, actionArgs);
  }

  public synchronized ShellResponse runCommand(
      NodeDetails node, Universe universe, String command) {
    List<String> actionArgs = new ArrayList<>();
    actionArgs.add("--command");
    actionArgs.add(command);
    return executeNodeAction(UniverseNodeAction.RUN_COMMAND, universe, node, actionArgs);
  }

  public synchronized ShellResponse runYsqlCommand(
      NodeDetails node, Universe universe, String dbName, String ysqlCommand) {
    return runYsqlCommand(node, universe, dbName, ysqlCommand, YSQL_COMMAND_DEFAULT_TIMEOUT_SEC);
  }

  public synchronized ShellResponse runYsqlCommand(
      NodeDetails node, Universe universe, String dbName, String ysqlCommand, int timeoutSec) {
    List<String> command = new ArrayList<>();
    command.add("timeout");
    command.add(String.valueOf(timeoutSec));
    command.add(getYbHomeDir(node, universe) + "/tserver/bin/ysqlsh");
    command.add("-h");
    Cluster cluster = universe.getUniverseDetails().getPrimaryCluster();
    if (cluster.userIntent.isYSQLAuthEnabled()) {
      command.add("$(dirname \"$(ls /tmp/.yb.*/.s.PGSQL.* | head -1)\")");
    } else {
      command.add(node.cloudInfo.private_ip);
    }
    command.add("-p");
    command.add(String.valueOf(node.ysqlServerRpcPort));
    command.add("-U");
    command.add("yugabyte");
    if (cluster.userIntent.enableClientToNodeEncrypt && !cluster.userIntent.enableYSQLAuth) {
      command.add("\"sslmode=require\"");
    }
    command.add("-d");
    command.add(dbName);
    command.add("-c");
    command.add("'" + ysqlCommand + "'");
    return runCommand(node, universe, String.join(" ", command));
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
   * Get deployment mode of node (on-prem/kubernetes/cloud provider)
   *
   * @param node - node to get info on
   * @param universe - the universe
   * @return Get deployment details
   */
  private Common.CloudType getNodeDeploymentMode(NodeDetails node, Universe universe) {
    if (node == null) {
      throw new RuntimeException("node must be nonnull");
    }
    UniverseDefinitionTaskParams.Cluster cluster =
        universe.getUniverseDetails().getClusterByUuid(node.placementUuid);
    return cluster.userIntent.providerType;
  }

  /**
   * Returns yb home directory for node
   *
   * @param node
   * @param universe
   * @return home directory
   */
  private String getYbHomeDir(NodeDetails node, Universe universe) {
    UUID providerUUID =
        UUID.fromString(
            universe.getUniverseDetails().getClusterByUuid(node.placementUuid).userIntent.provider);
    Provider provider = Provider.get(providerUUID);
    return provider.getYbHome();
  }

  private ShellResponse executeNodeAction(
      UniverseNodeAction nodeAction, Universe universe, NodeDetails node, List<String> actionArgs) {
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
    if (getNodeDeploymentMode(node, universe).equals(Common.CloudType.kubernetes)) {

      // Get namespace.  First determine isMultiAz.
      Provider provider = Provider.getOrBadRequest(providerUUID);
      boolean isMultiAz = PlacementInfoUtil.isMultiAZ(provider);
      String namespace =
          PlacementInfoUtil.getKubernetesNamespace(
              universe.getUniverseDetails().nodePrefix,
              isMultiAz ? AvailabilityZone.getOrBadRequest(node.azUuid).name : null,
              AvailabilityZone.get(node.azUuid).getUnmaskedConfig());

      commandArgs.add("k8s");
      commandArgs.add("--namespace");
      commandArgs.add(namespace);
    } else if (!getNodeDeploymentMode(node, universe).equals(Common.CloudType.unknown)) {
      AccessKey accessKey =
          AccessKey.getOrBadRequest(providerUUID, cluster.userIntent.accessKeyCode);
      commandArgs.add("ssh");
      commandArgs.add("--port");
      commandArgs.add(accessKey.getKeyInfo().sshPort.toString());
      commandArgs.add("--ip");
      commandArgs.add(node.cloudInfo.private_ip);
      commandArgs.add("--key");
      commandArgs.add(getAccessKey(node, universe));
    } else {
      throw new RuntimeException("Cloud type unknown");
    }
    commandArgs.add(nodeAction.name().toLowerCase());
    commandArgs.addAll(actionArgs);
    LOG.debug("Executing command: " + commandArgs);
    return shellProcessHandler.run(commandArgs, new HashMap<>(), true);
  }

  public enum UniverseNodeAction {
    RUN_COMMAND,
    DOWNLOAD_LOGS
  }
}
