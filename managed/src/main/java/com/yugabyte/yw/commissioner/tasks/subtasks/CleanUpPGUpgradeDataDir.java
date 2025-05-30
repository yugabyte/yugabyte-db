// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.google.common.collect.ImmutableList;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.params.ServerSubTaskParams;
import com.yugabyte.yw.common.KubernetesManagerFactory;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.List;
import java.util.Map;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class CleanUpPGUpgradeDataDir extends ServerSubTaskBase {

  private final NodeUniverseManager nodeUniverseManager;
  private final KubernetesManagerFactory kubernetesManagerFactory;

  public static class Params extends ServerSubTaskParams {}

  public static final String PG_UPGRADE_DIR = "/pg_upgrade_data*";

  @Inject
  protected CleanUpPGUpgradeDataDir(
      BaseTaskDependencies baseTaskDependencies,
      NodeUniverseManager nodeUniverseManager,
      KubernetesManagerFactory kubernetesManagerFactory) {
    super(baseTaskDependencies);
    this.nodeUniverseManager = nodeUniverseManager;
    this.kubernetesManagerFactory = kubernetesManagerFactory;
  }

  @Override
  public void run() {

    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    boolean isK8sUniverse =
        universe
            .getUniverseDetails()
            .getPrimaryCluster()
            .userIntent
            .providerType
            .equals(CloudType.kubernetes);
    for (NodeDetails node : universe.getMasters()) {
      if (isK8sUniverse) {
        cleanUpDirOnK8sPod(universe, node);
      } else {
        cleanUpDirOnVMNode(universe, node);
      }
    }
  }

  private void cleanUpDirOnVMNode(Universe universe, NodeDetails node) {
    String pgUpgradeDataDir = Util.getDataDirectoryPath(universe, node, config) + PG_UPGRADE_DIR;
    List<String> command = ImmutableList.of("rm", "-rf", pgUpgradeDataDir);
    ShellResponse response =
        nodeUniverseManager.runCommand(node, universe, command).processErrors();
    if (response.code != 0) {
      log.error(
          "Failed to clean up pg_upgrade_data directory on node {}, response: {}",
          node.cloudInfo.private_ip,
          response.getMessage());
      throw new RuntimeException(
          "Failed to clean up pg_upgrade_data directory on node " + node.cloudInfo.private_ip);
    }
  }

  private void cleanUpDirOnK8sPod(Universe universe, NodeDetails node) {
    Map<String, String> zoneConfig =
        CloudInfoInterface.fetchEnvVars(AvailabilityZone.getOrBadRequest(node.azUuid));
    String namespace = node.cloudInfo.kubernetesNamespace;
    String podName = node.cloudInfo.kubernetesPodName;
    kubernetesManagerFactory
        .getManager()
        .executeCommandInPodContainer(
            zoneConfig,
            namespace,
            podName,
            "yb-master",
            ImmutableList.of(
                "/bin/bash",
                "-c",
                "rm -rf " + Util.getDataDirectoryPath(universe, node, config) + PG_UPGRADE_DIR));
  }
}
