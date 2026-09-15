// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.rollback;

import com.google.inject.AbstractModule;
import com.google.inject.multibindings.MapBinder;
import com.yugabyte.yw.models.helpers.TaskType;

/**
 * Binds source {@link TaskType}s to {@link TaskRollbackComputer} implementations used by {@link
 * com.yugabyte.yw.common.CustomerTaskManager#rollbackCustomerTask}.
 */
public class TaskRollbackModule extends AbstractModule {
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
  }
}
