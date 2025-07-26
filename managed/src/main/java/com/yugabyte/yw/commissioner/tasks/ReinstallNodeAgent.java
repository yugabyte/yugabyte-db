// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.google.common.collect.ImmutableList;
import com.google.common.collect.Lists;
import com.google.common.collect.Sets;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ShellProcessContext;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import java.util.List;
import java.util.Objects;
import java.util.Set;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;

@Slf4j
@Abortable
public class ReinstallNodeAgent extends UniverseDefinitionTaskBase {

  @Inject
  protected ReinstallNodeAgent(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends NodeTaskParams {
    public Set<String> nodeNames;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    // This lock neither puts the universe in error state on failure nor freezes the universe. So
    // the checks can be run after locking.
    Universe universe = lockUniverse(-1);
    try {
      Integer parallelism =
          confGetter.getConfForScope(universe, UniverseConfKeys.nodeAgentReinstallParallelism);
      List<NodeDetails> nodeDetails =
          filterNodesForInstallNodeAgent(
                  universe, universe.getNodes(), true /* includeOnPremManual */)
              .stream()
              .filter(n -> n.state == NodeState.Live)
              .filter(
                  n ->
                      CollectionUtils.isEmpty(taskParams().nodeNames)
                          || taskParams().nodeNames.contains(n.getNodeName()))
              .collect(Collectors.toList());
      Set<String> targetNodeNames =
          CollectionUtils.isEmpty(taskParams().nodeNames)
              ? universe.getNodes().stream()
                  .map(NodeDetails::getNodeName)
                  .collect(Collectors.toSet())
              : Sets.newHashSet(taskParams().nodeNames);
      Set<String> allNodeNames =
          nodeDetails.stream().map(NodeDetails::getNodeName).collect(Collectors.toSet());
      if (targetNodeNames.size() != nodeDetails.size()) {
        Set<String> filteredNodes = Sets.difference(targetNodeNames, allNodeNames);
        log.error(
            "Some nodes {} are not eligible for installing node agent. Make sure they are Live and"
                + " node agent client is enabled",
            filteredNodes);
        throw new PlatformServiceException(
            BAD_REQUEST,
            "Some nodes are not eligible for installing node agent. Make sure they are Live and"
                + " node agent client is enabled");
      }
      // Check the connection first before making changes.
      createCheckSshConnectionTasks(nodeDetails)
          .setSubTaskGroupType(SubTaskGroupType.PreflightChecks);
      if (Util.isOnPremManualProvisioning(universe)) {
        ShellProcessContext shellContext =
            ShellProcessContext.builder().useSshConnectionOnly(true).build();
        // Check if user systemd is possible.
        createRunEnableLingerTask(universe, nodeDetails, shellContext);
        // Check if root systemd is managing yb-node-agent.service.
        List<String> cmd =
            ImmutableList.of(
                "bash",
                "-c",
                "systemctl list-unit-files yb-node-agent.service || echo 'NOT_FOUND'");
        createRunNodeCommandTask(
            universe,
            nodeDetails,
            cmd,
            (n, r) -> {
              String output =
                  r.processErrors("Failed to run command " + cmd).extractRunCommandOutput();
              if (!output.contains("NOT_FOUND")) {
                throw new RuntimeException(
                    "Root systemd is already managing node agent. Only user systemd is supported");
              }
            },
            shellContext);
      }
      Lists.partition(nodeDetails, parallelism)
          .forEach(
              list ->
                  createInstallNodeAgentTasks(universe, list, true /* reinstall */)
                      .setSubTaskGroupType(SubTaskGroupType.InstallingSoftware));
      // Shallow copy in a thread-safe set.
      Set<String> pendingTargetNodeNames = Sets.newConcurrentHashSet(targetNodeNames);
      // Wait for all nodes in the universe.
      createWaitForNodeAgentTasks(universe.getNodes())
          .setSubTaskGroupType(SubTaskGroupType.InstallingSoftware)
          .setAfterTaskRunHandler(
              (t, e) -> {
                String nodeName = t.getTaskParams().get("nodeName").textValue();
                if (pendingTargetNodeNames.contains(nodeName)) {
                  pendingTargetNodeNames.remove(nodeName);
                  return e;
                }
                // Do not fail for non-targeted nodes.
                return null;
              })
          .setAfterGroupRunListener(
              g -> {
                // Fail only if the ping fails for the targeted nodes.
                if (!pendingTargetNodeNames.isEmpty()) {
                  throw new RuntimeException(
                      String.format("Nodes %s did not respond to ping", pendingTargetNodeNames));
                }
              });
      // Update installNodeAgent property only if all the nodes have working node agents.
      createUpdateUniverseFieldsTask(u -> u.getUniverseDetails().installNodeAgent = false)
          .setSubTaskGroupType(SubTaskGroupType.InstallingSoftware)
          .setShouldRunPredicate(
              t ->
                  universe.getNodes().stream()
                          .map(n -> NodeAgent.maybeGetByIp(n.cloudInfo.private_ip).orElse(null))
                          .filter(Objects::nonNull)
                          .filter(n -> n.getState() == NodeAgent.State.READY)
                          .count()
                      == universe.getNodes().size());
      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(SubTaskGroupType.InstallingSoftware);
      getRunnableTask().runSubTasks();
    } finally {
      unlockUniverseForUpdate();
      log.info("Finished {} task.", getName());
    }
  }
}
