// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.google.common.annotations.VisibleForTesting;
import com.google.common.collect.Lists;
import com.google.common.collect.Sets;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.certmgmt.CertConfigType;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import java.util.function.Predicate;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;

@Slf4j
public class UpgradeNodeAgent extends UniverseDefinitionTaskBase {
  @Inject
  protected UpgradeNodeAgent(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends NodeTaskParams {
    public UUID certificateUuid;
    public Set<String> nodeNames;
    public boolean certsOnly;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void validateParams(boolean isFirstTry) {
    getCertificateInfoIfSpecified();
  }

  @Override
  protected void createPrecheckTasks(Universe universe) {
    List<NodeDetails> eligibleNodes = getEligibleNodesForUpgrade(universe);
    CertificateInfo certificateInfo = getCertificateInfoIfSpecified();
    if (certificateInfo != null) {
      // Basic validation for the root CA with the node certs on the DB.
      createCheckCertificateConfigTask(
          universe.getUniverseDetails().clusters,
          Set.copyOf(eligibleNodes),
          certificateInfo.getUuid(),
          null /* clientRootCA */,
          false /* enableClientToNodeEncrypt */,
          NodeManager.YUGABYTE_USER);
    }
  }

  private CertificateInfo getCertificateInfoIfSpecified() {
    CertificateInfo certificateInfo = null;
    if (taskParams().certificateUuid != null) {
      certificateInfo = CertificateInfo.getOrBadRequest(taskParams().certificateUuid);
      if (certificateInfo.getCertType() != CertConfigType.CustomCertHostPath
          && certificateInfo.getCertType() != CertConfigType.SelfSigned) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            "Only CustomCertHostPath or SelfSigned type certificate is supported for node"
                + " agent upgrade");
      }
    }
    return certificateInfo;
  }

  @VisibleForTesting
  List<NodeDetails> getEligibleNodesForUpgrade(Universe universe) {
    // If nodeNames is empty, it means all Live nodes in the universe. Otherwise, only the
    // specified nodes irrespective of the node state as connection pre-check is run.
    Predicate<NodeDetails> nodeFilter =
        CollectionUtils.isEmpty(taskParams().nodeNames)
            ? n -> n.state == NodeState.Live
            : n -> taskParams().nodeNames.contains(n.getNodeName());
    Customer customer = Customer.get(universe.getCustomerId());
    Set<String> nodeAgentIps =
        NodeAgent.getAll(customer.getUuid()).stream()
            .map(NodeAgent::getIp)
            .collect(Collectors.toSet());
    Set<String> targetNodeNames =
        CollectionUtils.isEmpty(taskParams().nodeNames)
            ? universe.getNodes().stream().map(NodeDetails::getNodeName).collect(Collectors.toSet())
            : Sets.newHashSet(taskParams().nodeNames);
    List<NodeDetails> eligibleNodes =
        universe.getNodes().stream()
            .filter(nodeFilter)
            .filter(
                n ->
                    n.cloudInfo != null
                        && n.cloudInfo.private_ip != null
                        && nodeAgentIps.contains(n.cloudInfo.private_ip))
            .collect(Collectors.toList());
    if (targetNodeNames.size() != eligibleNodes.size()) {
      Set<String> eligibleNodeNames =
          eligibleNodes.stream().map(NodeDetails::getNodeName).collect(Collectors.toSet());
      Set<String> filteredNodes = Sets.difference(targetNodeNames, eligibleNodeNames);
      log.error(
          "Some nodes {} are not eligible for upgrading node agent. Make sure they are Live and"
              + " node agent is installed",
          filteredNodes);
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Some nodes are not eligible for upgrading node agent. Make sure they are Live and node"
              + " agent is installed");
    }
    return eligibleNodes;
  }

  @Override
  public void run() {
    // This lock neither puts the universe in error state on failure nor freezes the universe. So
    // the checks can be run after locking.
    Universe universe = lockUniverse(-1);
    try {
      Integer parallelism =
          confGetter.getConfForScope(universe, UniverseConfKeys.nodeAgentReinstallParallelism);
      List<NodeDetails> eligibleNodes = getEligibleNodesForUpgrade(universe);
      Set<String> eligibleNodeNames =
          eligibleNodes.stream().map(NodeDetails::getNodeName).collect(Collectors.toSet());
      Lists.partition(eligibleNodes, parallelism)
          .forEach(
              list ->
                  createRunUpgradeNodeAgentTasks(
                          universe, list, taskParams().certificateUuid, taskParams().certsOnly)
                      .setSubTaskGroupType(SubTaskGroupType.InstallingSoftware));
      // Shallow copy in a thread-safe set.
      Set<String> pendingTargetNodeNames = Sets.newConcurrentHashSet(eligibleNodeNames);
      // Wait for the upgraded nodes.
      createWaitForNodeAgentTasks(eligibleNodes)
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
      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(SubTaskGroupType.InstallingSoftware);
      getRunnableTask().runSubTasks();
    } finally {
      unlockUniverseForUpdate();
      log.info("Finished {} task.", getName());
    }
  }
}
