// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import com.yugabyte.yw.commissioner.TaskExecutor.SubTaskGroup;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.subtasks.RunHooks;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.utils.NaturalOrderComparator;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Hook;
import com.yugabyte.yw.models.HookScope;
import com.yugabyte.yw.models.HookScope.TriggerType;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.ArrayList;
import java.util.Collection;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class HookInserter {

  private static final String ENABLE_SUDO_PATH = "yb.security.custom_hooks.enable_sudo";

  public static void addHookTrigger(
      TriggerType trigger,
      List<UUID> hookUUIDs,
      AbstractTaskBase task,
      UniverseTaskParams universeParams,
      Collection<NodeDetails> nodes) {
    if (!task.confGetter.getGlobalConf(GlobalConfKeys.enableCustomHooks)) return;
    List<Pair<Hook, Collection<NodeDetails>>> executionPlan =
        getExecutionPlan(trigger, hookUUIDs, universeParams, task.runtimeConfigFactory, nodes);

    for (Pair<Hook, Collection<NodeDetails>> singleHookPlan : executionPlan) {
      Hook hook = singleHookPlan.getFirst();
      Collection<NodeDetails> targetNodes = singleHookPlan.getSecond();

      // Create the hook script to run
      SubTaskGroup subTaskGroup =
          task.createSubTaskGroup(
              "Hook-" + task.userTaskUUID + "-" + hook.getName(), SubTaskGroupType.RunningHooks);
      for (NodeDetails node : targetNodes) {
        RunHooks.Params taskParams = new RunHooks.Params();
        taskParams.creatingUser = universeParams.creatingUser;
        taskParams.hook = hook;
        taskParams.hookPath =
            task.confGetter.getGlobalConf(GlobalConfKeys.ybTmpDirectoryPath)
                + "/"
                + node.nodeUuid
                + "-"
                + hook.getName();
        taskParams.trigger = trigger;
        taskParams.nodeName = node.nodeName;
        taskParams.nodeUuid = node.nodeUuid;
        taskParams.azUuid = node.azUuid;
        taskParams.setUniverseUUID(universeParams.getUniverseUUID());
        taskParams.parentTask = task.getClass().getSimpleName();
        RunHooks runHooks = AbstractTaskBase.createTask(RunHooks.class);
        runHooks.initialize(taskParams);
        subTaskGroup.addSubTask(runHooks);
      }

      task.getRunnableTask().addSubTaskGroup(subTaskGroup);
    }
  }

  public static void addHookTrigger(
      TriggerType trigger,
      AbstractTaskBase task,
      UniverseTaskParams universeParams,
      Collection<NodeDetails> nodes) {
    addHookTrigger(trigger, Collections.emptyList() /* hookUUIDs */, task, universeParams, nodes);
  }

  // Get all the hooks and their targets, and then order them in natural order.
  private static List<Pair<Hook, Collection<NodeDetails>>> getExecutionPlan(
      TriggerType trigger,
      List<UUID> hookUUIDs,
      UniverseTaskParams universeParams,
      RuntimeConfigFactory rConfig,
      Collection<NodeDetails> nodes) {
    boolean isSudoEnabled = rConfig.globalRuntimeConf().getBoolean(ENABLE_SUDO_PATH);
    List<Pair<Hook, Collection<NodeDetails>>> executionPlan =
        new ArrayList<Pair<Hook, Collection<NodeDetails>>>();
    UUID universeUUID = universeParams.getUniverseUUID();
    Universe universe = Universe.getOrBadRequest(universeUUID);
    UUID customerUUID = Customer.get(universe.getCustomerId()).getUuid();

    // Get global hooks
    HookScope globalScope = HookScope.getByTriggerScopeId(customerUUID, trigger, null, null, null);
    addHooksToExecutionPlan(executionPlan, globalScope, hookUUIDs, nodes, isSudoEnabled);

    // Get provider hooks
    // How:
    // 1. Bucket nodes by provider UUID
    // 2. Add the hooks to the excution plan
    Map<UUID, List<NodeDetails>> nodeProviderMap = new HashMap<>();
    for (NodeDetails node : nodes) {
      Cluster cluster = universe.getUniverseDetails().getClusterByUuid(node.placementUuid);
      UUID providerUUID = UUID.fromString(cluster.userIntent.provider);
      nodeProviderMap.computeIfAbsent(providerUUID, k -> new ArrayList<>()).add(node);
    }
    for (Map.Entry<UUID, List<NodeDetails>> entry : nodeProviderMap.entrySet()) {
      UUID providerUUID = entry.getKey();
      List<NodeDetails> providerNodes = entry.getValue();
      HookScope providerScope =
          HookScope.getByTriggerScopeId(customerUUID, trigger, null, providerUUID, null);
      addHooksToExecutionPlan(
          executionPlan, providerScope, hookUUIDs, providerNodes, isSudoEnabled);
    }

    // Get universe hooks
    HookScope universeScope =
        HookScope.getByTriggerScopeId(customerUUID, trigger, universeUUID, null, null);
    addHooksToExecutionPlan(executionPlan, universeScope, hookUUIDs, nodes, isSudoEnabled);

    // Get the cluster hooks
    Map<UUID, List<NodeDetails>> nodeClusterMap = new HashMap<>();
    for (NodeDetails node : nodes) {
      Cluster cluster = universe.getUniverseDetails().getClusterByUuid(node.placementUuid);
      nodeClusterMap.computeIfAbsent(cluster.uuid, k -> new ArrayList<>()).add(node);
    }
    for (Map.Entry<UUID, List<NodeDetails>> entry : nodeClusterMap.entrySet()) {
      UUID clusterUUID = entry.getKey();
      List<NodeDetails> clusterNodes = entry.getValue();
      HookScope clusterScope =
          HookScope.getByTriggerScopeId(customerUUID, trigger, universeUUID, null, clusterUUID);
      addHooksToExecutionPlan(executionPlan, clusterScope, hookUUIDs, clusterNodes, isSudoEnabled);
    }

    // Sort in natural order
    NaturalOrderComparator comparator = new NaturalOrderComparator();
    Collections.sort(
        executionPlan,
        (a, b) -> {
          return comparator.compare(a.getFirst().getName(), b.getFirst().getName());
        });

    return executionPlan;
  }

  private static void addHooksToExecutionPlan(
      List<Pair<Hook, Collection<NodeDetails>>> executionPlan,
      HookScope hookScope,
      List<UUID> hookUUIDs,
      Collection<NodeDetails> nodes,
      boolean isSudoEnabled) {
    if (hookScope == null) return;
    for (Hook hook : hookScope.getHooks()) {
      if (!isSudoEnabled && hook.isUseSudo()) {
        log.debug("Sudo execution is not enabled, ignoring {}", hook.getName());
        continue;
      }
      if (hookUUIDs != null && !hookUUIDs.isEmpty() && !hookUUIDs.contains(hook.getUuid())) {
        log.debug(
            "Hooks specified to run {} doesn't contain current hook {}, ignoring {}",
            hookUUIDs,
            hook.getUuid(),
            hook.getName());
        continue;
      }
      executionPlan.add(new Pair<>(hook, nodes));
    }
  }
}
