// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.KubernetesTaskBase;
import com.yugabyte.yw.common.ShellKubernetesManager;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class PodDisruptionBudgetPolicy extends KubernetesTaskBase {

  private final ShellKubernetesManager shellKubernetesManager;

  @Inject
  protected PodDisruptionBudgetPolicy(
      BaseTaskDependencies baseTaskDependencies, ShellKubernetesManager shellKubernetesManager) {
    super(baseTaskDependencies);
    this.shellKubernetesManager = shellKubernetesManager;
  }

  public static class Params extends UniverseDefinitionTaskParams {
    public boolean deletePDB;
    public boolean reCreatePDB;
  }

  public Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public String getName() {
    return String.format("%s(deletePDB=%s)", super.getName(), taskParams().deletePDB);
  }

  @Override
  public void run() {
    try {
      log.info("Running {}", getName());
      Universe universe = getUniverse();
      if (taskParams().deletePDB || taskParams().reCreatePDB) {
        shellKubernetesManager.deletePodDisruptionBudget(universe);
      }
      if (!taskParams().deletePDB) {
        shellKubernetesManager.createPodDisruptionBudget(universe);
      }
    } catch (Exception e) {
      log.error("Error executing task {} with error={}.", getName(), e.getMessage(), e);
      throw e;
    }
    log.info("Finished {} task.", getName());
  }
}
