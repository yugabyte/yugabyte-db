// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.rollback;

import com.google.common.collect.ImmutableMap;
import com.google.inject.AbstractModule;
import com.google.inject.multibindings.MapBinder;
import com.yugabyte.yw.models.helpers.TaskType;

/**
 * Binds source {@link TaskType}s to {@link TaskRollbackComputer} implementations used by {@link
 * com.yugabyte.yw.common.CustomerTaskManager#rollbackCustomerTask}.
 */
public class TaskRollbackModule extends AbstractModule {

  /**
   * Source placement-lock task to the 1:1 rollback {@link TaskType} allowed while the universe is
   * frozen failed. Static so {@code UniverseTaskBase.getAllowedTasksOnFailure} does not need Guice.
   * Software upgrade is omitted: VM vs Kubernetes is chosen at compute time.
   */
  public static final ImmutableMap<TaskType, TaskType> PLACEMENT_ROLLBACK_TASK_TYPES =
      ImmutableMap.of(
          TaskType.EditUniverse,
          EditUniverseRollbackComputer.ROLLBACK_TASK_TYPE,
          TaskType.EditKubernetesUniverse,
          EditKubernetesUniverseRollbackComputer.ROLLBACK_TASK_TYPE,
          TaskType.AddNodeToUniverse,
          AddNodeToUniverseRollbackComputer.ROLLBACK_TASK_TYPE,
          TaskType.ResizeNode,
          ResizeNodeRollbackComputer.ROLLBACK_TASK_TYPE);

  @Override
  protected void configure() {
    MapBinder<TaskType, TaskRollbackComputer> mapBinder =
        MapBinder.newMapBinder(binder(), TaskType.class, TaskRollbackComputer.class);
    mapBinder.addBinding(TaskType.SwitchoverDrConfig).to(SwitchoverDrConfigRollbackComputer.class);
    mapBinder.addBinding(TaskType.SoftwareUpgradeYB).to(SoftwareUpgradeRollbackComputer.class);
    mapBinder
        .addBinding(TaskType.SoftwareKubernetesUpgradeYB)
        .to(SoftwareUpgradeRollbackComputer.class);
    mapBinder.addBinding(TaskType.EditUniverse).to(EditUniverseRollbackComputer.class);
    mapBinder
        .addBinding(TaskType.EditKubernetesUniverse)
        .to(EditKubernetesUniverseRollbackComputer.class);
    mapBinder.addBinding(TaskType.AddNodeToUniverse).to(AddNodeToUniverseRollbackComputer.class);
    mapBinder.addBinding(TaskType.ResizeNode).to(ResizeNodeRollbackComputer.class);
  }
}
