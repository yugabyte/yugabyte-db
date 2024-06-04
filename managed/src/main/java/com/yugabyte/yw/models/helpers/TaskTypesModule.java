/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.models.helpers;

import com.google.inject.AbstractModule;
import com.google.inject.Provides;
import com.google.inject.multibindings.MapBinder;
import com.yugabyte.yw.commissioner.ITask;
import java.util.HashMap;
import java.util.Map;

public class TaskTypesModule extends AbstractModule {

  @Override
  protected void configure() {
    MapBinder<TaskType, ITask> taskMapBinder =
        MapBinder.newMapBinder(binder(), TaskType.class, ITask.class);
    TaskType.filteredValues()
        .forEach(taskType -> taskMapBinder.addBinding(taskType).to(taskType.getTaskClass()));
  }

  @Provides
  Map<Class<? extends ITask>, TaskType> inverseTaskMap() {
    final HashMap<Class<? extends ITask>, TaskType> inverseMap = new HashMap<>();
    TaskType.filteredValues()
        .forEach(taskType -> inverseMap.put(taskType.getTaskClass(), taskType));
    return inverseMap;
  }
}
