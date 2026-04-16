// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.ShellProcessContext;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.List;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

@Slf4j
public class CheckSshConnection extends NodeTaskBase {
  private final ShellProcessContext defaultShellContext =
      ShellProcessContext.builder().useSshConnectionOnly(true).logCmdOutput(true).build();

  @Inject
  protected CheckSshConnection(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  public void run() {
    Universe universe = getUniverse();
    NodeDetails node = universe.getNode(taskParams().nodeName);
    if (node == null) {
      String errMsg =
          String.format(
              "Node %s is not found in the universe %s(%s)",
              taskParams().nodeName, universe.getName(), universe.getUniverseUUID());
      log.error(errMsg);
      throw new IllegalStateException(errMsg);
    }
    Cluster cluster = universe.getUniverseDetails().getClusterByNodeName(taskParams().nodeName);
    if (cluster == null) {
      String errMsg =
          String.format(
              "Cluster for node %s is not found in the universe %s(%s)",
              taskParams().nodeName, universe.getName(), universe.getUniverseUUID());
      log.error(errMsg);
      throw new IllegalStateException(errMsg);
    }
    ShellProcessContext shellContext = defaultShellContext;
    Provider provider = Provider.getOrBadRequest(UUID.fromString(cluster.userIntent.provider));
    if (provider.getCloudCode() != CloudType.onprem || !provider.getDetails().skipProvisioning) {
      // Set custom user for CSPs and onprem non-manual, otherwise yugabyte user is the default.
      String sshUser = imageBundleUtil.findEffectiveSshUser(provider, universe, node);
      if (StringUtils.isNotEmpty(node.sshUserOverride)) {
        sshUser = node.sshUserOverride;
      }
      if (StringUtils.isNotEmpty(sshUser)) {
        shellContext = defaultShellContext.toBuilder().sshUser(sshUser).build();
      }
    }
    List<String> testCmd = ImmutableList.of("/bin/bash", "-c", "echo 'test'");
    nodeUniverseManager
        .runCommand(node, universe, testCmd, shellContext)
        .processErrors("SSH connection failed for user: " + shellContext.getSshUser());
  }
}
