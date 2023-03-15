// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import com.google.inject.Singleton;
import com.yugabyte.yw.commissioner.TaskExecutor.TaskExecutionListener;
import com.yugabyte.yw.common.operator.KubernetesOperatorStatusUpdater;
import com.yugabyte.yw.common.ProviderEditRestrictionManager;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import java.util.HashMap;
import java.util.Map;
import java.util.Map.Entry;
import java.util.Optional;
import java.util.UUID;
import java.util.function.Consumer;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class Listener implements TaskExecutionListener {
  private Consumer<TaskInfo> beforeTaskConsumer;
  private ProviderEditRestrictionManager providerEditRestrictionManager;

  public static final Logger LOG = LoggerFactory.getLogger(Commissioner.class);

  public Listener(
      ProviderEditRestrictionManager providerEditRestrictionManager,
      Consumer<TaskInfo> beforeTaskConsumer) {
    this.providerEditRestrictionManager = providerEditRestrictionManager;
    this.beforeTaskConsumer = beforeTaskConsumer;
  }

  @Override
  public void beforeTask(TaskInfo taskInfo) {
    LOG.info("About to execute task {}", taskInfo);
    if (beforeTaskConsumer != null) {
      beforeTaskConsumer.accept(taskInfo);
    }
  }

  @Override
  public void afterTask(TaskInfo taskInfo, Throwable t) {
    LOG.info("Task {} is completed", taskInfo);
    providerEditRestrictionManager.onTaskFinished(taskInfo.getTaskUUID());
  }

  @Override
  public void afterSubtaskGroup(
      String name, TaskInfo taskInfo, Map<UUID, Universe> kubernetesOperatorMap, Throwable t) {
    LOG.info("Subtask group {} is completed", name);
    if (taskInfo.getUniverseUuid() != null) {
      updateAfterOperatorTask(name, taskInfo, kubernetesOperatorMap, t);
    }
  }

  @Override
  public void afterParentTask(
      String name, TaskInfo taskInfo, Map<UUID, Universe> kubernetesOperatorMap, Throwable t) {
    LOG.info("Parent task {} is completed", name);
    if (taskInfo.getUniverseUuid() != null) {
      updateAfterOperatorTask(name, taskInfo, kubernetesOperatorMap, t);
      // remove uuid after each operation.
      kubernetesOperatorMap.remove(taskInfo.getUniverseUuid());
    }
  }

  private void updateAfterOperatorTask(
      String name, TaskInfo taskInfo, Map<UUID, Universe> kubernetesOperatorMap, Throwable t) {
    if (kubernetesOperatorMap.containsKey(taskInfo.getUniverseUuid())
        && kubernetesOperatorMap
            .get(taskInfo.getUniverseUuid())
            .getUniverseDetails()
            .isKubernetesOperatorControlled) {
      updateYBUniverseStatus(
          name, taskInfo, kubernetesOperatorMap.get(taskInfo.getUniverseUuid()), t);
    } else if (!kubernetesOperatorMap.containsKey(taskInfo.getUniverseUuid())) {
      UUID universeUUID = taskInfo.getUniverseUuid();
      Optional<Universe> u = Universe.maybeGet(universeUUID);

      u.ifPresent(
          universe -> {
            kubernetesOperatorMap.put(universeUUID, universe);
            if (kubernetesOperatorMap
                .get(taskInfo.getUniverseUuid())
                .getUniverseDetails()
                .isKubernetesOperatorControlled) {
              updateYBUniverseStatus(name, taskInfo, universe, t);
            }
          });
    }
  }

  private void updateYBUniverseStatus(
      String name, TaskInfo taskInfo, Universe universe, Throwable t) {
    try {
      // Updating Kubernetes Custom Resource (if done through operator).
      LOG.info("ybUniverseStatus info: {}: {}", name, taskInfo.getTaskState().toString());
      String status = (t != null ? "Failed" : "Success");
      KubernetesOperatorStatusUpdater.updateStatus(universe, name.concat(" ").concat(status));
    } catch (Exception e) {
      LOG.warn("Error in updating Kubernetes Operator Universe", e);
    }
  }
};
