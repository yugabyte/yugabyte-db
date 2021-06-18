package com.yugabyte.yw.common;

import com.google.inject.Singleton;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;

import javax.xml.soap.Node;
import java.util.*;

@Singleton
public class NodeUniverseManager extends DevopsBase {
  public static final String DOWNLOAD_LOGS_SSH_SCRIPT = "bin/support_package.py";

  @Override
  protected String getCommandType() {
    return null;
  }

  public synchronized ShellResponse downloadNodeLogs(
      NodeDetails node, Universe universe, String targetLocalFile) {
    List<String> commandArgs = new ArrayList<>();

    commandArgs.add(PY_WRAPPER);
    commandArgs.add(DOWNLOAD_LOGS_SSH_SCRIPT);
    UniverseDefinitionTaskParams.Cluster cluster =
        universe.getUniverseDetails().getClusterByUuid(node.placementUuid);
    UUID providerUUID = UUID.fromString(cluster.userIntent.provider);
    if (getNodeDeploymentMode(node, universe).equals(Common.CloudType.kubernetes)) {

      // Get namespace.  First determine isMultiAz.
      Provider provider = Provider.get(providerUUID);
      boolean isMultiAz = PlacementInfoUtil.isMultiAZ(provider);
      String namespace =
          PlacementInfoUtil.getKubernetesNamespace(
              universe.getUniverseDetails().nodePrefix,
              isMultiAz ? AvailabilityZone.get(node.azUuid).name : null,
              AvailabilityZone.get(node.azUuid).getConfig());

      commandArgs.add("k8s");
      commandArgs.add("--namespace");
      commandArgs.add(namespace);
    } else if (!getNodeDeploymentMode(node, universe).equals(Common.CloudType.unknown)) {
      AccessKey accessKey = AccessKey.get(providerUUID, cluster.userIntent.accessKeyCode);
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
    if (node.isMaster) {
      commandArgs.add("--is_master");
    }
    commandArgs.add("--node_name");
    commandArgs.add(node.nodeName);
    commandArgs.add("--yb_home_dir");
    commandArgs.add(getYbHomeDir(node, universe));
    commandArgs.add("--target_local_file");
    commandArgs.add(targetLocalFile);
    LOG.debug("Executing command: " + commandArgs);
    return shellProcessHandler.run(commandArgs, new HashMap<>(), true);
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
  public Common.CloudType getNodeDeploymentMode(NodeDetails node, Universe universe) {
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
}
