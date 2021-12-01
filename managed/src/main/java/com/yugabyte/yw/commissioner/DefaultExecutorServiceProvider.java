// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.commissioner;

import com.google.common.util.concurrent.ThreadFactoryBuilder;
import com.yugabyte.yw.common.PlatformExecutorFactory;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.ThreadFactory;
import javax.inject.Inject;
import javax.inject.Singleton;

@Singleton
public class DefaultExecutorServiceProvider implements ExecutorServiceProvider {

  private final PlatformExecutorFactory platformExecutorFactory;

  private final Map<TaskType, ExecutorService> executorServices = new ConcurrentHashMap<>();

  @Inject
  public DefaultExecutorServiceProvider(PlatformExecutorFactory platformExecutorFactory) {
    this.platformExecutorFactory = platformExecutorFactory;
  }

  public ExecutorService getExecutorServiceFor(TaskType taskType) {
    return executorServices.computeIfAbsent(
        taskType,
        t -> {
          ThreadFactory namedThreadFactory =
              new ThreadFactoryBuilder().setNameFormat("TaskPool-" + taskType + "-%d").build();
          return platformExecutorFactory.createExecutor("task", namedThreadFactory);
        });
  }
}
