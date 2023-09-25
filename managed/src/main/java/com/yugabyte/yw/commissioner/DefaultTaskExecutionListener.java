// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import com.yugabyte.yw.commissioner.TaskExecutor.TaskExecutionListener;
import com.yugabyte.yw.common.ProviderEditRestrictionManager;
import com.yugabyte.yw.models.TaskInfo;
import java.util.function.Consumer;
import lombok.extern.slf4j.Slf4j;
import org.slf4j.MDC;

@Slf4j
public class DefaultTaskExecutionListener implements TaskExecutionListener {
  private final Consumer<TaskInfo> beforeTaskConsumer;
  private final ProviderEditRestrictionManager providerEditRestrictionManager;

  public DefaultTaskExecutionListener(
      ProviderEditRestrictionManager providerEditRestrictionManager,
      Consumer<TaskInfo> beforeTaskConsumer) {
    this.providerEditRestrictionManager = providerEditRestrictionManager;
    this.beforeTaskConsumer = beforeTaskConsumer;
  }

  @Override
  public void beforeTask(TaskInfo taskInfo) {
    MDC.put(Commissioner.TASK_ID, taskInfo.getTaskUUID().toString());
    log.info("About to execute task {}", taskInfo);
    if (beforeTaskConsumer != null) {
      beforeTaskConsumer.accept(taskInfo);
    }
  }

  @Override
  public void afterTask(TaskInfo taskInfo, Throwable t) {
    MDC.remove(Commissioner.TASK_ID);
    log.info("Task {} is completed", taskInfo);
    providerEditRestrictionManager.onTaskFinished(taskInfo.getTaskUUID());
  }
}
