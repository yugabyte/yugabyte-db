package com.yugabyte.yw.common;

import com.google.inject.Singleton;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.UUID;
import java.util.concurrent.TimeUnit;

@Singleton
public class NodeUniverseManager extends DevopsBase {
  public static final int YSQL_COMMAND_DEFAULT_TIMEOUT_SEC = 20;
  public static final String NODE_ACTION_SSH_SCRIPT = "bin/run_node_action.py";
  public static final String CERTS_DIR = "/yugabyte-tls-config";
  public static final String K8S_CERTS_DIR = "/opt/certs/yugabyte";

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

  public synchronized ShellResponse downloadNodeFile(
      NodeDetails node,
      Universe universe,
      String ybHomeDir,
      String sourceNodeFile,
      String targetLocalFile) {
    List<String> actionArgs = new ArrayList<>();
    // yb_home_dir denotes a custom starting directory for the remote file. (Eg: ~/, /mnt/d0, etc.)
    actionArgs.add("--yb_home_dir");
    actionArgs.add(ybHomeDir);
    actionArgs.add("--source_node_files");
    actionArgs.add(sourceNodeFile);
    actionArgs.add("--target_local_file");
    actionArgs.add(targetLocalFile);
    return executeNodeAction(UniverseNodeAction.DOWNLOAD_FILE, universe, node, actionArgs);
  }

  public synchronized ShellResponse runCommand(
      NodeDetails node, Universe universe, String command) {
    List<String> actionArgs = new ArrayList<>();
    actionArgs.add("--command");
    actionArgs.add(command);
    return executeNodeAction(UniverseNodeAction.RUN_COMMAND, universe, node, actionArgs);
  }

  public synchronized ShellResponse runYbAdminCommand(
      NodeDetails node, Universe universe, String ybAdminCommand, long timeoutSec) {
    List<String> command = new ArrayList<>();
    command.add("/usr/bin/timeout");
    command.add(String.valueOf(timeoutSec));
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
    return runCommand(node, universe, String.join(" ", command));
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
    Cluster cluster = universe.getUniverseDetails().getPrimaryCluster();
    if (cluster.userIntent.enableClientToNodeEncrypt && !cluster.userIntent.enableYSQLAuth) {
      command.add("env sslmode=\"require\"");
    }
    command.add(getYbHomeDir(node, universe) + "/tserver/bin/ysqlsh");
    command.add("-h");
    if (cluster.userIntent.isYSQLAuthEnabled()) {
      command.add("$(dirname \"$(ls /tmp/.yb.*/.s.PGSQL.* | head -1)\")");
    } else {
      command.add(node.cloudInfo.private_ip);
    }
    command.add("-p");
    command.add(String.valueOf(node.ysqlServerRpcPort));
    command.add("-U");
    command.add("yugabyte");
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
  public String getYbHomeDir(NodeDetails node, Universe universe) {
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
      // TODO(bhavin192): this might need an updated when we have
      // multiple releases in one namespace.
      String kubeconfig =
          PlacementInfoUtil.getConfigPerNamespace(
                  cluster.placementInfo, universe.getUniverseDetails().nodePrefix, provider)
              .get(namespace);
      if (kubeconfig == null) {
        throw new RuntimeException("kubeconfig cannot be null");
      }

      commandArgs.add("k8s");
      commandArgs.add("--namespace");
      commandArgs.add(namespace);
      commandArgs.add("--kubeconfig");
      commandArgs.add(kubeconfig);
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

  private String getCertsDir(Universe universe, NodeDetails node) {
    if (getNodeDeploymentMode(node, universe).equals(Common.CloudType.kubernetes)) {
      return K8S_CERTS_DIR;
    }
    return getYbHomeDir(node, universe) + CERTS_DIR;
  }

  public enum UniverseNodeAction {
    RUN_COMMAND,
    DOWNLOAD_LOGS,
    DOWNLOAD_FILE
  }
}
