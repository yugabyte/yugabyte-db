// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import static com.yugabyte.yw.common.PlatformExecutorFactory.SHUTDOWN_TIMEOUT_MINUTES;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.util.concurrent.MoreExecutors;
import com.google.common.util.concurrent.ThreadFactoryBuilder;
import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.TaskExecutor.RunnableTask;
import com.yugabyte.yw.commissioner.TaskExecutor.SubTaskGroup;
import com.yugabyte.yw.commissioner.TaskExecutor.TaskCache;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.PlatformExecutorFactory;
import com.yugabyte.yw.common.RestoreManagerYb;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.TableManager;
import com.yugabyte.yw.common.TableManagerYb;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.alerts.AlertConfigurationService;
import com.yugabyte.yw.common.backuprestore.BackupHelper;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.inject.StaticInjectorHolder;
import com.yugabyte.yw.common.metrics.MetricService;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.ITaskParams;
import java.time.Duration;
import java.util.UUID;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.ThreadFactory;
import java.util.concurrent.TimeUnit;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import play.Application;
import play.libs.Json;

@Slf4j
public abstract class AbstractTaskBase implements ITask {

  private static final String SLEEP_DISABLED_PATH = "yb.tasks.disabled_timeouts";

  // The threadpool on which the subtasks are executed.
  private ExecutorService executor;

  // The params for this task.
  protected ITaskParams taskParams;

  // The UUID of this task.
  protected UUID taskUUID;

  // The UUID of the top-level user-facing task at the top of Task tree. Eg. CreateUniverse, etc.
  protected UUID userTaskUUID;

  // A field used to send additional information with prometheus metric associated with this task
  public String taskInfo = "";

  protected final Application application;
  protected final play.Environment environment;
  protected final Config config;
  protected final ConfigHelper configHelper;
  protected final RuntimeConfigFactory runtimeConfigFactory;
  protected final RuntimeConfGetter confGetter;
  protected final MetricService metricService;
  protected final AlertConfigurationService alertConfigurationService;
  protected final YBClientService ybService;
  protected final RestoreManagerYb restoreManagerYb;
  protected final TableManager tableManager;
  protected final TableManagerYb tableManagerYb;
  private final PlatformExecutorFactory platformExecutorFactory;
  private final TaskExecutor taskExecutor;
  private final Commissioner commissioner;
  protected final HealthChecker healthChecker;
  protected final NodeManager nodeManager;
  protected final BackupHelper backupHelper;

  @Inject
  protected AbstractTaskBase(BaseTaskDependencies baseTaskDependencies) {
    this.application = baseTaskDependencies.getApplication();
    this.environment = baseTaskDependencies.getEnvironment();
    this.config = baseTaskDependencies.getConfig();
    this.configHelper = baseTaskDependencies.getConfigHelper();
    this.runtimeConfigFactory = baseTaskDependencies.getRuntimeConfigFactory();
    this.confGetter = baseTaskDependencies.getConfGetter();
    this.metricService = baseTaskDependencies.getMetricService();
    this.alertConfigurationService = baseTaskDependencies.getAlertConfigurationService();
    this.ybService = baseTaskDependencies.getYbService();
    this.restoreManagerYb = baseTaskDependencies.getRestoreManagerYb();
    this.tableManager = baseTaskDependencies.getTableManager();
    this.tableManagerYb = baseTaskDependencies.getTableManagerYb();
    this.platformExecutorFactory = baseTaskDependencies.getExecutorFactory();
    this.taskExecutor = baseTaskDependencies.getTaskExecutor();
    this.commissioner = baseTaskDependencies.getCommissioner();
    this.healthChecker = baseTaskDependencies.getHealthChecker();
    this.nodeManager = baseTaskDependencies.getNodeManager();
    this.backupHelper = baseTaskDependencies.getBackupHelper();
  }

  protected ITaskParams taskParams() {
    return taskParams;
  }

  @Override
  public void initialize(ITaskParams params) {
    this.taskParams = params;
  }

  @Override
  public String getName() {
    return this.getClass().getSimpleName();
  }

  @Override
  public JsonNode getTaskDetails() {
    return Json.toJson(taskParams);
  }

  @Override
  public String toString() {
    return getName() + " : details=" + getTaskDetails();
  }

  @Override
  public abstract void run();

  @Override
  public synchronized void terminate() {
    if (executor != null && !executor.isShutdown()) {
      MoreExecutors.shutdownAndAwaitTermination(
          executor, SHUTDOWN_TIMEOUT_MINUTES, TimeUnit.MINUTES);
      executor = null;
    }
  }

  protected synchronized ExecutorService getOrCreateExecutorService() {
    if (executor == null) {
      log.info("Executor name: {}", getExecutorPoolName());
      ThreadFactory namedThreadFactory =
          new ThreadFactoryBuilder().setNameFormat("TaskPool-" + getName() + "-%d").build();
      executor = platformExecutorFactory.createExecutor(getExecutorPoolName(), namedThreadFactory);
    }
    return executor;
  }

  protected String getExecutorPoolName() {
    return "task";
  }

  @Override
  public void setTaskUUID(UUID taskUUID) {
    this.taskUUID = taskUUID;
  }

  @Override
  public void setUserTaskUUID(UUID userTaskUUID) {
    this.userTaskUUID = userTaskUUID;
  }

  @Override
  public boolean isFirstTry() {
    return taskParams() == null || taskParams().getPreviousTaskUUID() == null;
  }

  @Override
  public void validateParams(boolean isFirstTry) {}

  /**
   * We would try to parse the shell response message as JSON and return JsonNode
   *
   * @param response: ShellResponse object
   * @return JsonNode: Json formatted shell response message
   */
  public JsonNode parseShellResponseAsJson(ShellResponse response) {
    return Util.convertStringToJson(response.message);
  }

  /**
   * Creates task with appropriate dependency injection
   *
   * @param taskClass task class
   * @return Task instance with injected dependencies
   */
  public static <T extends ITask> T createTask(Class<T> taskClass) {
    return StaticInjectorHolder.injector().instanceOf(TaskExecutor.class).createTask(taskClass);
  }

  public int getSleepMultiplier() {
    try {
      return config.getBoolean(SLEEP_DISABLED_PATH) ? 0 : 1;
    } catch (Exception e) {
      return 1;
    }
  }

  protected TaskExecutor getTaskExecutor() {
    return taskExecutor;
  }

  protected Commissioner getCommissioner() {
    return commissioner;
  }

  // Returns the RunnableTask instance to which SubTaskGroup instances can be added and run.
  protected RunnableTask getRunnableTask() {
    return getTaskExecutor().getRunnableTask(userTaskUUID);
  }

  /**
   * Clears current task queue and runs tasks added by lambda.
   *
   * @param setTaskQueueRunnable
   */
  protected void setTaskQueueAndRun(Runnable setTaskQueueRunnable) {
    getRunnableTask().reset();
    setTaskQueueRunnable.run();
    getRunnableTask().runSubTasks();
  }

  protected SubTaskGroup createSubTaskGroup(String name) {
    return createSubTaskGroup(name, SubTaskGroupType.Invalid);
  }

  protected SubTaskGroup createSubTaskGroup(String name, boolean ignoreErrors) {
    return createSubTaskGroup(name, SubTaskGroupType.Invalid);
  }

  protected SubTaskGroup createSubTaskGroup(String name, SubTaskGroupType subTaskGroupType) {
    return createSubTaskGroup(name, subTaskGroupType, false);
  }

  // Returns a SubTaskGroup to which subtasks can be added.
  protected SubTaskGroup createSubTaskGroup(
      String name, SubTaskGroupType subTaskGroupType, boolean ignoreErrors) {
    SubTaskGroup subTaskGroup =
        getTaskExecutor().createSubTaskGroup(name, subTaskGroupType, ignoreErrors);
    subTaskGroup.setSubTaskExecutor(getOrCreateExecutorService());
    return subTaskGroup;
  }

  // Abort-aware wait function makes the current thread to wait until the timeout or the abort
  // signal is received. It can be a replacement for Thread.sleep in subtasks.
  protected void waitFor(Duration duration) {
    getRunnableTask().waitFor(duration);
  }

  protected UUID getUserTaskUUID() {
    return userTaskUUID;
  }

  protected UUID getTaskUUID() {
    return taskUUID;
  }

  protected TaskCache getTaskCache() {
    return getRunnableTask().getTaskCache();
  }
}
