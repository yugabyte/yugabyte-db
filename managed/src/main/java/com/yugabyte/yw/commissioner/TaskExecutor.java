// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner;

import static com.google.common.base.Preconditions.checkArgument;
import static com.google.common.base.Preconditions.checkNotNull;
import static com.yugabyte.yw.models.helpers.CommonUtils.getElapsedTime;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;
import static play.mvc.Http.Status.SERVICE_UNAVAILABLE;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.api.client.util.Throwables;
import com.google.common.annotations.VisibleForTesting;
import com.google.common.base.Preconditions;
import com.google.common.collect.ImmutableMap;
import com.google.common.collect.ImmutableSet;
import com.google.common.collect.Sets;
import com.google.inject.Provider;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.CanRollback;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.ITask.TaskVersion;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.common.DrainableMap;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.RedactingService;
import com.yugabyte.yw.common.RedactingService.RedactionTarget;
import com.yugabyte.yw.common.ShutdownHookHandler;
import com.yugabyte.yw.common.TaskExecutionException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.ha.PlatformReplicationManager;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.ScheduleTask;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.TaskInfo.State;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.models.helpers.YBAError;
import com.yugabyte.yw.models.helpers.YBAError.Code;
import io.prometheus.metrics.core.metrics.Summary;
import io.prometheus.metrics.model.registry.PrometheusRegistry;
import java.time.Duration;
import java.time.Instant;
import java.time.temporal.ChronoUnit;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Queue;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.CancellationException;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ConcurrentLinkedQueue;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Future;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;
import java.util.function.BiFunction;
import java.util.function.Consumer;
import java.util.function.Predicate;
import java.util.function.Supplier;
import java.util.stream.Collectors;
import java.util.stream.IntStream;
import javax.annotation.Nullable;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.Builder;
import lombok.Getter;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import org.apache.commons.lang3.exception.ExceptionUtils;
import play.libs.Json;

/**
 * TaskExecutor is the executor service for tasks and their subtasks. It is very similar to the
 * current SubTaskGroupQueue and SubTaskGroup.
 *
 * <p>A task is submitted by first creating a RunnableTask.
 *
 * <pre>
 * RunnableTask runnableTask = taskExecutor.createRunnableTask(taskType, taskParams);
 * UUID taskUUID = taskExecutor.submit(runnableTask, executor);
 * </pre>
 *
 * The RunnableTask instance is first retrieved in the run() method of the task.
 *
 * <pre>
 * RunnableTask runnableTask = taskExecutor.getRunnableTask(UUID).
 * </pre>
 *
 * This is similar to the current implementation of SubTaskGroupQueue queue = new
 * SubTaskGroupQueue(UUID).
 *
 * <p>The subtasks are added by first adding them to their groups and followed by adding the groups
 * to the RunnableTask instance.
 *
 * <pre>
 * void createProvisionNodes(List<Node> nodes, SubTaskGroupType groupType) {
 *   SubTasksGroup group = taskExecutor.createSubTaskGroup("provision-nodes", groupType);
 *   for (Node node : nodes) {
 *     // Create the subtask instance and initialize.
 *     ITask subTask = createAndInitSubTask(node);
 *     // Add the concurrent subtasks to the group.
 *     group.addSubTask(subTask);
 *   }
 *   runnableTask.addSubTaskGroup(group);
 * }
 * </pre>
 *
 * After all the subtasks are added, runSubTasks() is invoked in the run() method of the task. e.g
 *
 * <pre>
 * // Run method of task
 * void run() {
 *   // Same as the current queue = new SubTaskGroupQueue(UUID);
 *   runnableTask = taskExecutor.getRunnableTask(UUID).
 *   // Creates subtask group which is then added to runnableTask.
 *   createTasks1(nodes);
 *   createTasks2(nodes);
 *   createTasks3(nodes);
 *   // Same as the current queue.run();
 *   runnableTask.runSubTasks();
 * }
 * </pre>
 */
@Singleton
@Slf4j
public class TaskExecutor {

  // This is a map from the task types to the classes.
  private final Map<TaskType, Provider<ITask>> taskTypeMap;

  // Task futures are waited for this long before checking abort status.
  private static final long TASK_SPIN_WAIT_INTERVAL_MS = 2000;

  // Max size of the callstack for task creator thread.
  private static final int MAX_TASK_CREATOR_CALLSTACK_SIZE = 15;

  private static final String TASK_EXECUTION_SKIPPED_LABEL = "skipped";

  // Default wait timeout for subtasks to complete since the abort call.
  private final Duration defaultAbortTaskTimeout = Duration.ofSeconds(30);

  // ExecutorService provider for subtasks if explicit ExecutorService
  // is set for the subtasks in a task.
  private final ExecutorServiceProvider executorServiceProvider;

  // A map from task UUID to its RunnableTask while it is running.
  private final DrainableMap<UUID, RunnableTask> runnableTasks = new DrainableMap<>();

  // A utility for Platform HA.
  private final PlatformReplicationManager replicationManager;

  private final Map<Class<? extends ITask>, TaskType> inverseTaskTypeMap;

  private final RuntimeConfGetter runtimeConfGetter;

  private final AtomicBoolean isShutdown = new AtomicBoolean();

  private final String taskOwner;

  // Skip or perform abortable check for subtasks.
  private final boolean skipSubTaskAbortableCheck;

  private static final String COMMISSIONER_TASK_WAITING_SEC_METRIC =
      "ybp_commissioner_task_waiting_sec";

  private static final String COMMISSIONER_TASK_EXECUTION_SEC_METRIC =
      "ybp_commissioner_task_execution_sec";

  private static final Summary COMMISSIONER_TASK_WAITING_SEC =
      buildSummary(
          COMMISSIONER_TASK_WAITING_SEC_METRIC,
          "Duration between task creation and execution",
          KnownAlertLabels.TASK_TYPE.labelName(),
          KnownAlertLabels.PARENT_TASK_TYPE.labelName());

  private static final Summary COMMISSIONER_TASK_EXECUTION_SEC =
      buildSummary(
          COMMISSIONER_TASK_EXECUTION_SEC_METRIC,
          "Duration of task execution",
          KnownAlertLabels.TASK_TYPE.labelName(),
          KnownAlertLabels.RESULT.labelName(),
          KnownAlertLabels.PARENT_TASK_TYPE.labelName(),
          TASK_EXECUTION_SKIPPED_LABEL);

  private static Summary buildSummary(String name, String description, String... labelNames) {
    return Summary.builder()
        .name(name)
        .help(description)
        .quantile(0.5, 0.05)
        .quantile(0.9, 0.01)
        .labelNames(labelNames)
        .register(PrometheusRegistry.defaultRegistry);
  }

  // This writes the waiting time metric.
  private static void writeTaskWaitMetric(
      Map<String, String> taskLabels, Instant scheduledTime, Instant startTime) {
    COMMISSIONER_TASK_WAITING_SEC
        .labelValues(
            taskLabels.get(KnownAlertLabels.TASK_TYPE.labelName()),
            taskLabels.get(KnownAlertLabels.PARENT_TASK_TYPE.labelName()))
        .observe(getElapsedTime(scheduledTime, startTime, ChronoUnit.SECONDS));
  }

  // This writes the execution time metric.
  private static void writeTaskStateMetric(
      Map<String, String> taskLabels,
      Instant startTime,
      Instant endTime,
      State state,
      boolean isTaskSkipped) {
    COMMISSIONER_TASK_EXECUTION_SEC
        .labelValues(
            taskLabels.get(KnownAlertLabels.TASK_TYPE.labelName()),
            state.name(),
            taskLabels.get(KnownAlertLabels.PARENT_TASK_TYPE.labelName()),
            String.valueOf(isTaskSkipped))
        .observe(getElapsedTime(startTime, endTime, ChronoUnit.SECONDS));
  }

  // It looks for the annotation starting from the current class to its super classes until it
  // finds. If it is not found, it returns false, else the value of enabled is returned. It is
  // possible to override an annotation already defined in the superclass.
  static boolean isTaskAbortable(Class<? extends ITask> taskClass) {
    checkNotNull(taskClass, "Task class must be non-null");
    Optional<Abortable> optional = CommonUtils.isAnnotatedWith(taskClass, Abortable.class);
    return optional.map(Abortable::enabled).orElse(false);
  }

  // It looks for the annotation starting from the current class to its super classes until it
  // finds. If it is not found, it returns false, else the value of enabled is returned. It is
  // possible to override an annotation already defined in the superclass.
  static boolean isTaskRetryable(Class<? extends ITask> taskClass) {
    checkNotNull(taskClass, "Task class must be non-null");
    Optional<Retryable> optional = CommonUtils.isAnnotatedWith(taskClass, Retryable.class);
    return optional.map(Retryable::enabled).orElse(false);
  }

  /**
   * It returns a boolean showing whether the task can be rolled back or not.
   *
   * <p>See {@link TaskExecutor#isTaskRetryable(Class)}
   */
  static boolean canTaskRollback(Class<? extends ITask> taskClass) {
    checkNotNull(taskClass, "Task class must be non-null");
    Optional<CanRollback> optional = CommonUtils.isAnnotatedWith(taskClass, CanRollback.class);
    return optional.map(CanRollback::enabled).orElse(false);
  }

  /**
   * Returns the task type for the given task class.
   *
   * @param taskClass the given task class.
   * @return task type for the task class.
   */
  public TaskType getTaskType(Class<? extends ITask> taskClass) {
    checkNotNull(taskClass, "Task class must be non-null");
    return inverseTaskTypeMap.get(taskClass);
  }

  public boolean isShutdown() {
    return isShutdown.get();
  }

  @Inject
  public TaskExecutor(
      ShutdownHookHandler shutdownHookHandler,
      ExecutorServiceProvider executorServiceProvider,
      PlatformReplicationManager replicationManager,
      Map<TaskType, Provider<ITask>> taskTypeMap,
      Map<Class<? extends ITask>, TaskType> inverseTaskTypeMap,
      RuntimeConfGetter runtimeConfGetter) {
    this.executorServiceProvider = executorServiceProvider;
    this.replicationManager = replicationManager;
    this.taskOwner = Util.getHostname();
    this.skipSubTaskAbortableCheck = true;
    shutdownHookHandler.addShutdownHook(
        this, taskExecutor -> taskExecutor.shutdown(Duration.ofMinutes(2)), 100 /* weight */);
    this.taskTypeMap = taskTypeMap;
    this.inverseTaskTypeMap = inverseTaskTypeMap;
    this.runtimeConfGetter = runtimeConfGetter;
  }

  // Shuts down the task executor.
  // It assumes that the executor services will
  // also be shutdown gracefully.
  public boolean shutdown(Duration timeout) {
    if (isShutdown.compareAndSet(false, true)) {
      log.info("TaskExecutor is shutting down");
      runnableTasks.sealMap();
      Instant abortTime = Instant.now();
      runnableTasks.forEach(
          (uuid, runnable) -> {
            runnable.setAbortTime(abortTime);
            runnable.cancelWaiterIfAborted();
          });
    }
    try {
      // Wait for all the RunnableTask to be done.
      // A task in runnableTasks map is removed when it is cancelled due to executor shutdown or
      // when it is completed.
      return runnableTasks.waitForEmpty(timeout);
    } catch (InterruptedException e) {
      log.error("Wait for task completion interrupted", e);
    }
    log.debug("TaskExecutor shutdown in time");
    return false;
  }

  private void checkTaskExecutorState() {
    if (isShutdown.get()) {
      throw new PlatformServiceException(SERVICE_UNAVAILABLE, "TaskExecutor is shutting down");
    }
  }

  private void checkHAFollowerState() {
    if (HighAvailabilityConfig.isFollower()) {
      throw new IllegalStateException("Can not submit task on HA follower");
    }
  }

  /** Task params for creating a RunnableTask. */
  @Builder
  @Getter
  public static class TaskParams {
    private final TaskType taskType;
    private final ITaskParams taskParams;
    @Nullable private final UUID taskUuid;
  }

  /**
   * Instantiates the task for the task class.
   *
   * @param taskClass the task class.
   * @return the task.
   */
  public <T extends ITask> T createTask(Class<T> taskClass) {
    checkNotNull(taskClass, "Task class must be set");
    return taskClass.cast(taskTypeMap.get(getTaskType(taskClass)).get());
  }

  /**
   * Creates a RunnableTask instance for a task with the given parameters.
   *
   * @param params the runnable task creation params.
   * @return runnable task.
   */
  public RunnableTask createRunnableTask(TaskParams params) {
    checkNotNull(params, "Creation params cannot be null");
    checkNotNull(params.getTaskType(), "Task type must be set");
    checkNotNull(params.getTaskParams(), "Task params must be set");
    ITaskParams taskParams = params.getTaskParams();
    ITask task = taskTypeMap.get(params.getTaskType()).get();
    task.initialize(taskParams);
    return createRunnableTask(task, null, taskParams.getPreviousTaskUUID());
  }

  /* Creates a RunnableTask instance for the given task. */
  @VisibleForTesting
  RunnableTask createRunnableTask(ITask task, @Nullable UUID taskUUID) {
    return createRunnableTask(task, taskUUID, null /* previousTaskUUID */);
  }

  /* Creates a RunnableTask instance for the given task. */
  @VisibleForTesting
  RunnableTask createRunnableTask(
      ITask task, @Nullable UUID taskUUID, @Nullable UUID previousTaskUUID) {
    TaskInfo taskInfo = createTaskInfo(task, taskUUID, previousTaskUUID);
    taskInfo.setPosition(-1);
    taskInfo.save();
    task.setTaskUUID(taskInfo.getUuid());
    task.setUserTaskUUID(taskInfo.getUuid());
    log.info(
        "Task {}({}) with version {}",
        taskInfo.getTaskType(),
        taskInfo.getUuid(),
        taskInfo.getTaskVersion());
    return new RunnableTask(task, taskInfo.getUuid());
  }

  /**
   * Submits a RunnableTask to this TaskExecutor with the given ExecutorService for execution. If
   * the task has subtasks, they are submitted to this RunnableTask in its run() method.
   *
   * @param runnableTask the RunnableTask instance.
   * @param taskExecutorService the ExecutorService for this task.
   */
  public UUID submit(RunnableTask runnableTask, ExecutorService taskExecutorService) {
    checkNotNull(runnableTask, "Task runnable must not be null");
    try {
      checkNotNull(taskExecutorService, "Task executor service must not be null");
      checkTaskExecutorState();
      checkHAFollowerState();
      ITask task = runnableTask.getTask();
      try {
        task.validateParams(task.isFirstTry());
      } catch (PlatformServiceException e) {
        log.error("Params validation failed for task " + task, e);
        throw e;
      } catch (Exception e) {
        log.error("Params validation failed for task " + task, e);
        throw new PlatformServiceException(BAD_REQUEST, e.getMessage());
      }
      UUID taskUUID = runnableTask.getTaskUUID();
      runnableTasks.put(taskUUID, runnableTask);
      try {
        runnableTask.updateScheduledTime();
        runnableTask.future = taskExecutorService.submit(runnableTask);
        return taskUUID;
      } catch (Exception e) {
        // Update task state on submission failure.
        runnableTasks.remove(taskUUID);
        log.error("Error occurred in submitting the task", e);
        if (e instanceof RejectedExecutionException) {
          throw new PlatformServiceException(
              SERVICE_UNAVAILABLE, "Task submission failed as too many tasks are running");
        }
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR, "Error occurred during task submission for execution");
      }
    } catch (Exception e) {
      runnableTask.updateTaskDetailsOnError(TaskInfo.State.Failure, e);
      if (e instanceof PlatformServiceException) {
        throw (PlatformServiceException) e;
      }
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, e.getMessage());
    }
  }

  // This waits for the parent task to complete indefinitely.
  public void waitForTask(UUID taskUUID) {
    waitForTask(taskUUID, null);
  }

  // This waits for the parent task to complete within the timeout.
  public void waitForTask(UUID taskUUID, Duration timeout) {
    Optional<RunnableTask> optional = maybeGetRunnableTask(taskUUID);
    if (!optional.isPresent()) {
      return;
    }
    RunnableTask runnableTask = optional.get();
    try {
      if (timeout == null || timeout.isZero()) {
        runnableTask.future.get();
      } else {
        runnableTask.future.get(timeout.toMillis(), TimeUnit.MILLISECONDS);
      }
    } catch (ExecutionException e) {
      Throwables.propagate(e.getCause());
    } catch (Throwable e) {
      Throwables.propagate(e);
    }
  }

  @VisibleForTesting
  boolean isTaskRunning(UUID taskUUID) {
    return runnableTasks.containsKey(taskUUID);
  }

  /**
   * It signals to abort a task if it is already running. When a running task is aborted
   * asynchronously, the task state changes to 'Aborted'. It does not validate if the UUID
   * represents a valid task and returns an empty Optional if the task is either not running or not
   * found.
   *
   * @param taskUUID UUID of the task.
   * @param force skip some checks like abortable if it is set.
   * @return returns an optional TaskInfo that is present if the task is already found running.
   */
  public Optional<TaskInfo> abort(UUID taskUUID, boolean force) {
    log.info("Aborting task {}", taskUUID);
    Optional<RunnableTask> optional = maybeGetRunnableTask(taskUUID);
    if (!optional.isPresent()) {
      log.info("Task {} is not found. It is either completed or non-existing", taskUUID);
      return Optional.empty();
    }
    RunnableTask runnableTask = optional.get();
    ITask task = runnableTask.getTask();
    if (!force && !isTaskAbortable(task.getClass())) {
      throw new RuntimeException("Task " + task.getName() + " is not abortable");
    }
    runnableTask.abort(Duration.ZERO);
    // Update the task state in the memory and DB.
    runnableTask.compareAndSetTaskState(
        Sets.immutableEnumSet(State.Initializing, State.Created, State.Running), State.Abort);
    return Optional.of(runnableTask.getTaskInfo());
  }

  /**
   * Creates a SubTaskGroup with the given name.
   *
   * @param name the name of the group.
   * @return SubTaskGroup
   */
  public SubTaskGroup createSubTaskGroup(String name) {
    return createSubTaskGroup(name, SubTaskGroupType.Configuring, false);
  }

  /**
   * Creates a SubTaskGroup with the given parameters.
   *
   * @param name the name of the group.
   * @param executorService the executorService to run the tasks for this group.
   */
  public SubTaskGroup createSubTaskGroup(String name, ExecutorService executorService) {
    return createSubTaskGroup(name, executorService, false);
  }

  /**
   * Creates a SubTaskGroup with the given parameters.
   *
   * @param name the name of the group.
   * @param executorService the executorService to run the tasks for this group.
   * @param ignoreErrors ignore individual subtask error until the all the subtasks in the group are
   *     executed if it is set.
   */
  public SubTaskGroup createSubTaskGroup(
      String name, ExecutorService executorService, boolean ignoreErrors) {
    SubTaskGroup subTaskGroup =
        createSubTaskGroup(name, SubTaskGroupType.Configuring, ignoreErrors);
    subTaskGroup.setSubTaskExecutor(executorService);
    return subTaskGroup;
  }

  /**
   * Creates a SubTaskGroup to which subtasks can be added for concurrent execution.
   *
   * @param name the name of the group.
   * @param subTaskGroupType the type/phase of the subtasks group.
   * @param ignoreErrors ignore individual subtask error until the all the subtasks in the group are
   *     executed if it is set.
   * @return SubTaskGroup
   */
  public SubTaskGroup createSubTaskGroup(
      String name, SubTaskGroupType subTaskGroupType, boolean ignoreErrors) {
    return new SubTaskGroup(name, subTaskGroupType, ignoreErrors);
  }

  @VisibleForTesting
  TaskInfo createTaskInfo(ITask task, @Nullable UUID taskUUID, @Nullable UUID previousTaskUUID) {
    TaskType taskType = getTaskType(task.getClass());
    // Create a new task info object.
    TaskInfo taskInfo = null;
    if (taskUUID != null) {
      taskInfo = TaskInfo.maybeGet(taskUUID).orElse(null);
    }
    if (taskInfo == null) {
      taskInfo = new TaskInfo(taskType, null);
    } else {
      taskInfo.setTaskState(State.Created);
    }
    // Annotate the task with another version if there is a breaking change.
    // We must take care not to break the runtime info.
    int taskVersion =
        CommonUtils.isAnnotatedWith(taskType.getTaskClass(), TaskVersion.class)
            .map(v -> v.version())
            .orElse(ITask.DEFAULT_TASK_VERSION);
    // The runtime config is internal and must not be disabled unless absolutely necessary.
    if (previousTaskUUID != null
        && runtimeConfGetter.getGlobalConf(GlobalConfKeys.enableTaskRuntimeInfoOnRetry)) {
      TaskInfo previousTaskInfo = TaskInfo.getOrBadRequest(previousTaskUUID);
      if (taskVersion != previousTaskInfo.getTaskVersion()) {
        log.info("Inherting runtime task info from the previous task {}", previousTaskUUID);
        taskInfo.inherit(previousTaskInfo);
        if (log.isDebugEnabled() && taskInfo.getRuntimeInfo() != null) {
          log.debug(
              "Inherited runtime info from the previous task{}: {}",
              previousTaskUUID,
              Json.toJson(taskInfo.getRuntimeInfo()).toPrettyString());
        }
      } else {
        log.info(
            "Could not inherit runtime info due to different versions - current: {}, previous: {}",
            taskVersion,
            previousTaskInfo.getTaskVersion());
      }
    } else {
      log.info("Did not inherit runtime info due to unsatisfied conditions");
    }
    // Set the task params.
    taskInfo.setTaskParams(
        RedactingService.filterSecretFields(task.getTaskParams(), RedactionTarget.APIS));
    // Set the owner info.
    taskInfo.setOwner(taskOwner);
    taskInfo.setTaskVersion(taskVersion);
    taskInfo.setYbaVersion(Util.getYbaVersion());
    return taskInfo;
  }

  /**
   * Returns the current RunnableTask instance for the given task UUID. Subtasks can be submitted to
   * this instance and run. It throws IllegalStateException if the task is not present.
   *
   * @param taskUUID the task UUID.
   */
  public RunnableTask getRunnableTask(UUID taskUUID) {
    Optional<RunnableTask> optional = maybeGetRunnableTask(taskUUID);
    return optional.orElseThrow(
        () -> new IllegalStateException(String.format("Task(%s) is not present", taskUUID)));
  }

  /**
   * Returns an optional of the RunnableTask instance for the given task UUID if present.
   *
   * @param taskUUID the task UUID.
   * @return optional of the RunnableTask.
   */
  public Optional<RunnableTask> maybeGetRunnableTask(UUID taskUUID) {
    return Optional.ofNullable(runnableTasks.get(taskUUID));
  }

  /**
   * Listener to get called when a task is executed. This is useful for testing to throw exception
   * before a task is executed. Throwing CancellationException in beforeTask aborts the task.
   */
  @FunctionalInterface
  public interface TaskExecutionListener {
    default void beforeTask(TaskInfo taskInfo) {}

    void afterTask(TaskInfo taskInfo, Throwable t);
  }

  /**
   * A placeholder for a group of subtasks to be executed concurrently later. Subtasks requiring
   * concurrent executions are added to a group which is then added to the RunnableTask instance for
   * the task.
   */
  public class SubTaskGroup {
    private final Set<RunnableSubTask> subTasks = ConcurrentHashMap.newKeySet();
    private final String name;
    private final boolean ignoreErrors;
    private final AtomicInteger numCompletedTasks;
    // This predicate is invoked right before every subtask in this group.
    private final AtomicReference<Predicate<ITask>> shouldRunPredicateRef;
    private final AtomicReference<BiFunction<ITask, Throwable, Throwable>> afterTaskRunHandlerRef;
    private final AtomicReference<Consumer<SubTaskGroup>> afterGroupRunListenerRef;

    // Parent task runnable to which this group belongs.
    private volatile RunnableTask runnableTask;
    // Group position which is same as that of the subtasks.
    private int position;
    // Optional executor service for the subtasks.
    private ExecutorService executorService;
    private SubTaskGroupType subTaskGroupType = SubTaskGroupType.Configuring;

    // It is instantiated internally.
    private SubTaskGroup(String name, SubTaskGroupType subTaskGroupType, boolean ignoreErrors) {
      this.name = name;
      this.subTaskGroupType = subTaskGroupType;
      this.ignoreErrors = ignoreErrors;
      this.numCompletedTasks = new AtomicInteger();
      this.shouldRunPredicateRef = new AtomicReference<>();
      this.afterTaskRunHandlerRef = new AtomicReference<>();
      this.afterGroupRunListenerRef = new AtomicReference<>();
    }

    /**
     * Adds a subtask to this SubTaskGroup. It adds the subtask in memory and is not run yet. When
     * the this SubTaskGroup is added to the RunnableTask, the subtasks are persisted.
     *
     * @param subTask subTask the subtask.
     */
    public void addSubTask(ITask subTask) {
      checkNotNull(subTask, "Subtask must be non-null");
      int subTaskCount = getSubTaskCount();
      log.info("Adding task #{}: {}", subTaskCount, subTask.getName());
      if (log.isDebugEnabled()) {
        JsonNode redactedTask =
            RedactingService.filterSecretFields(subTask.getTaskParams(), RedactionTarget.LOGS);
        log.debug(
            "Details for task #{}: {} details= {}", subTaskCount, subTask.getName(), redactedTask);
      }
      subTasks.add(new RunnableSubTask(this, subTask));
    }

    /**
     * Sets an optional ExecutorService for the subtasks in this group. If it is not set, an
     * ExecutorService is created by ExecutorServiceProvider for the parent task type.
     *
     * @param executorService the ExecutorService.
     */
    public void setSubTaskExecutor(ExecutorService executorService) {
      this.executorService = executorService;
    }

    public String getName() {
      return name;
    }

    public SubTaskGroupType getSubTaskGroupType() {
      return subTaskGroupType;
    }

    /** Returns the optional ExecutorService for the subtasks in this group. */
    private ExecutorService getSubTaskExecutorService() {
      return executorService;
    }

    private void setRunnableTaskContext(RunnableTask runnableTask, int position) {
      this.runnableTask = runnableTask;
      this.position = position;
      for (RunnableSubTask runnable : subTasks) {
        runnable.setRunnableTaskContext(runnableTask, position);
      }
    }

    // Submits the subtasks in the group to the ExecutorService.
    private void submitSubTasks() {
      for (RunnableSubTask runnable : subTasks) {
        runnable.executeWith(executorService);
      }
    }

    // Removes the completed subtask from the iterator.
    private void removeCompletedSubTask(
        Iterator<RunnableSubTask> taskIterator,
        RunnableSubTask runnableSubTask,
        Throwable throwable) {
      if (throwable != null) {
        log.error("Error occurred in subtask " + runnableSubTask.getTaskInfo(), throwable);
      }
      taskIterator.remove();
      numCompletedTasks.incrementAndGet();
    }

    // Wait for all the subtasks to complete. In this method, the state updates on
    // exceptions are done for tasks which are not yet running and exception occurs.
    private void waitForSubTasks(boolean abortOnFailure) {
      UUID parentTaskUUID = runnableTask.getTaskUUID();
      Instant waitStartTime = Instant.now();
      List<RunnableSubTask> runnableSubTasks =
          this.subTasks.stream().filter(t -> t.future != null).collect(Collectors.toList());

      Throwable anyEx = null;
      while (runnableSubTasks.size() > 0) {
        Iterator<RunnableSubTask> iter = runnableSubTasks.iterator();

        // Start round-robin check on the task completion to give fair share to each subtask.
        while (iter.hasNext()) {
          RunnableSubTask runnableSubTask = iter.next();
          Future<?> future = runnableSubTask.future;
          try {
            future.get(TASK_SPIN_WAIT_INTERVAL_MS, TimeUnit.MILLISECONDS);
            removeCompletedSubTask(iter, runnableSubTask, null);
          } catch (ExecutionException e) {
            // Ignore state update because this exception is thrown
            // during the task execution and is already taken care
            // by RunnableSubTask.
            anyEx = (anyEx != null) ? anyEx : e.getCause();
            removeCompletedSubTask(iter, runnableSubTask, e.getCause());
            // Call parent task abort if abortOnFailure set.
            if (abortOnFailure && !ignoreErrors) {
              runnableTask.setAbortTime(Instant.now());
              runnableTask.cancelWaiterIfAborted();
            }
          } catch (TimeoutException e) {
            // The exception is ignored if the elapsed time has not surpassed
            // the time limit.
            Duration elapsed = Duration.between(waitStartTime, Instant.now());
            if (log.isTraceEnabled()) {
              log.trace("Task {} has taken {}ms", parentTaskUUID, elapsed.toMillis());
            }
            Duration timeout = runnableSubTask.getTimeLimit();
            // If the subtask execution takes long, it is interrupted.
            if (!timeout.isZero() && elapsed.compareTo(timeout) > 0) {
              anyEx = e;
              future.cancel(true);
              // Report failure to the parent task.
              // Update the subtask state to aborted if the execution timed out.
              runnableSubTask.updateTaskDetailsOnError(TaskInfo.State.Aborted, e);
              removeCompletedSubTask(iter, runnableSubTask, e);
            } else if (skipSubTaskAbortableCheck
                || isTaskAbortable(runnableSubTask.getTask().getClass())) {
              if (runnableSubTask.isAbortTimeReached(defaultAbortTaskTimeout)) {
                // Cancel waiter if it was not done previously.
                runnableTask.cancelWaiterIfAborted();
                future.cancel(true);
                // Report aborted to the parent task.
                // Update the subtask state to aborted if the execution timed out.
                Throwable thisEx =
                    new CancellationException(
                        "Task " + runnableSubTask.getTaskType() + " is aborted");
                anyEx = (anyEx != null) ? anyEx : thisEx;
                runnableSubTask.updateTaskDetailsOnError(TaskInfo.State.Aborted, thisEx);
                removeCompletedSubTask(iter, runnableSubTask, anyEx);
              } else if (runnableSubTask.isAbortTimeReached(Duration.ZERO)) {
                // Cancel waiter first.
                runnableTask.cancelWaiterIfAborted();
              }
            }

          } catch (CancellationException e) {
            anyEx = (anyEx != null) ? anyEx : e;
            runnableSubTask.updateTaskDetailsOnError(TaskInfo.State.Aborted, e);
            removeCompletedSubTask(iter, runnableSubTask, e);
          } catch (InterruptedException e) {
            future.cancel(true);
            Throwable thisEx = new CancellationException(e.getMessage());
            anyEx = (anyEx != null) ? anyEx : thisEx;
            runnableSubTask.updateTaskDetailsOnError(TaskInfo.State.Aborted, thisEx);
            removeCompletedSubTask(iter, runnableSubTask, anyEx);
          } catch (Throwable th) {
            anyEx = th;
            runnableSubTask.updateTaskDetailsOnError(TaskInfo.State.Failure, th);
            removeCompletedSubTask(iter, runnableSubTask, th);
          }
        }
      }
      Duration elapsed = Duration.between(waitStartTime, Instant.now());
      log.debug("{}: wait completed in {}ms", title(), elapsed.toMillis());
      if (anyEx != null) {
        Throwables.propagate(anyEx);
      }
    }

    private void handleAfterGroupRun() {
      Consumer<SubTaskGroup> consumer = afterGroupRunListenerRef.get();
      if (consumer != null) {
        consumer.accept(this);
      }
    }

    private Throwable handleAfterTaskRun(RunnableSubTask runnableSubTask, Throwable t) {
      if (t != null && ExceptionUtils.hasCause(t, CancellationException.class)) {
        return new CancellationException(t.getMessage());
      }
      BiFunction<ITask, Throwable, Throwable> function = afterTaskRunHandlerRef.get();
      return function == null ? t : function.apply(runnableSubTask.getTask(), t);
    }

    /**
     * Sets the SubTaskGroupType for this SubTaskGroup.
     *
     * @param subTaskGroupType the SubTaskGroupType.
     * @return the SubTaskGroup instance.
     */
    public SubTaskGroup setSubTaskGroupType(SubTaskGroupType subTaskGroupType) {
      if (subTaskGroupType == null) {
        return this;
      }
      log.info("Setting subtask({}) group type to {}", name, subTaskGroupType);
      this.subTaskGroupType = subTaskGroupType;
      for (RunnableSubTask runnable : subTasks) {
        runnable.setSubTaskGroupType(subTaskGroupType);
      }
      return this;
    }

    public int getSubTaskCount() {
      return subTasks.size();
    }

    public int getCompletedTasksCount() {
      return numCompletedTasks.get();
    }

    public Set<UUID> getSubTaskUuids() {
      return subTasks.stream()
          .map(RunnableSubTask::getTaskUUID)
          .collect(ImmutableSet.toImmutableSet());
    }

    /**
     * This allows subtasks to be skipped based on additional conditions after they are already
     * added. The subtasks appear in the list of subtasks but are not run.
     */
    public SubTaskGroup setShouldRunPredicate(Predicate<ITask> predicate) {
      Preconditions.checkNotNull(predicate, "ShouldRun predicate cannot be null");
      shouldRunPredicateRef.set(predicate);
      return this;
    }

    /**
     * Set a custom handler to be invoked for a task after the task is executed successfully or
     * unsuccessfully. For failed execution, the function is called with the exception. The handler
     * can choose to ignore it by returning null. The default behavior is to fail the task on
     * exception.
     *
     * @param handler the handler function returning the exception to be thrown on failure. If it is
     *     null, the error is ignored and the task is marked success.
     * @return the current subtask group.
     */
    public SubTaskGroup setAfterTaskRunHandler(BiFunction<ITask, Throwable, Throwable> handler) {
      Preconditions.checkNotNull(handler, "After-task-run handler cannot be null");
      afterTaskRunHandlerRef.set(handler);
      return this;
    }

    /**
     * Set a listener after all the subtasks in the group ran successfully (no exception).
     *
     * @param listener the listener which will be invoked with the current subTaskGroup.
     * @return the current subtask group.
     */
    public SubTaskGroup setAfterGroupRunListener(Consumer<SubTaskGroup> listener) {
      Preconditions.checkNotNull(listener, "After-subTaskGroup-run listener cannot be null");
      afterGroupRunListenerRef.set(listener);
      return this;
    }

    private String title() {
      return String.format(
          "SubTaskGroup %s of type %s at position %d", name, subTaskGroupType.name(), position);
    }

    @Override
    public String toString() {
      return String.format(
          "%s: completed %d out of %d tasks", title(), getCompletedTasksCount(), getSubTaskCount());
    }
  }

  /** A simple cache for caching task runtime data. */
  public static class TaskCache {
    private final Map<String, JsonNode> data = new ConcurrentHashMap<>();

    public void put(String key, JsonNode value) {
      data.put(key, value);
    }

    public JsonNode get(String key) {
      return data.get(key);
    }

    public <T> void putObject(String key, T object) {
      data.put(key, Json.toJson(object));
    }

    public <T> T get(String key, Class<T> clazz) {
      JsonNode node = get(key);
      if (node == null || node.isNull()) {
        return null;
      }
      return Json.fromJson(node, clazz);
    }

    public Set<String> keys() {
      return data.keySet();
    }

    public JsonNode delete(String key) {
      return data.remove(key);
    }

    public void clear() {
      data.clear();
    }

    public int size() {
      return data.size();
    }
  }

  /**
   * Abstract implementation of a task runnable which handles the state update after the task has
   * started running. Synchronization is on the this object for taskInfo.
   */
  public abstract class AbstractRunnableTask implements Runnable {
    private final ITask task;
    // Timeout limit for this task.
    private final Duration timeLimit;
    private final String[] creatorCallstack;

    private Instant taskScheduledTime;
    private Instant taskStartTime;
    private Instant taskCompletionTime;

    // This is set only when subtask group is added to the parent task.
    private volatile UUID taskUuid;

    // Future of the task that is set after it is submitted to the ExecutorService.
    Future<?> future = null;

    protected AbstractRunnableTask(ITask task) {
      this.task = task;
      this.taskScheduledTime = Instant.now();

      Duration duration = Duration.ZERO;
      JsonNode jsonNode = task.getTaskParams();
      if (jsonNode != null && !jsonNode.isNull()) {
        JsonNode timeLimitJsonNode = jsonNode.get("timeLimitMins");
        if (timeLimitJsonNode != null && !timeLimitJsonNode.isNull()) {
          long timeLimitMins = Long.parseLong(timeLimitJsonNode.asText());
          duration = Duration.ofMinutes(timeLimitMins);
        }
      }
      timeLimit = duration;
      if (log.isDebugEnabled()) {
        StackTraceElement[] elements = Thread.currentThread().getStackTrace();
        // Track who creates this task. Skip the first three which contain getStackTrace and
        // runnable task creation (concrete and abstract classes).
        creatorCallstack =
            IntStream.range(3, elements.length)
                .limit(MAX_TASK_CREATOR_CALLSTACK_SIZE)
                .mapToObj(idx -> elements[idx].toString())
                .toArray(String[]::new);
      } else {
        creatorCallstack = new String[0];
      }
    }

    public ITask getTask() {
      return task;
    }

    public TaskInfo getTaskInfo() {
      return TaskInfo.getOrBadRequest(taskUuid);
    }

    @VisibleForTesting
    String[] getCreatorCallstack() {
      return creatorCallstack;
    }

    // State and error message updates to tasks are done in this method instead of waitForSubTasks
    // because nobody is waiting for the parent task.
    @Override
    public void run() {
      Throwable t = null;
      taskStartTime = Instant.now();
      boolean isTaskSkipped = false;
      Map<String, String> taskLabels = this.getTaskMetricLabels();
      long queuedTimeMs = getElapsedTime(taskScheduledTime, taskStartTime, ChronoUnit.MILLIS);
      try {
        if (log.isDebugEnabled()) {
          log.debug("Task {} waited for {}ms", task.getName(), queuedTimeMs);
        }
        writeTaskWaitMetric(taskLabels, taskScheduledTime, taskStartTime);
        TaskInfo.updateInTxn(getTaskUUID(), tf -> tf.setQueuedTimeMs(queuedTimeMs));
        publishBeforeTask();
        if (isAbortTimeReached(Duration.ZERO)) {
          throw new CancellationException("Task " + task.getName() + " is aborted");
        }
        if (shouldRun()) {
          TaskInfo.updateInTxn(getTaskUUID(), tf -> tf.setTaskState(TaskInfo.State.Running));
          log.debug("Invoking run() of task {}", task.getName());
          task.run();
        } else {
          isTaskSkipped = true;
          log.info("Skipping task {} beause it is set to not run", getTaskInfo());
        }
      } catch (Throwable e) {
        t = e;
      } finally {
        t = handleAfterRun(t);
        if (t == null) {
          TaskInfo.updateInTxn(getTaskUUID(), tf -> tf.setTaskState(TaskInfo.State.Success));
        } else if (ExceptionUtils.hasCause(t, CancellationException.class)) {
          updateTaskDetailsOnError(TaskInfo.State.Aborted, t);
        } else {
          updateTaskDetailsOnError(TaskInfo.State.Failure, t);
        }
        taskCompletionTime = Instant.now();
        long executionTimeMs = getElapsedTime(taskStartTime, taskCompletionTime, ChronoUnit.MILLIS);
        if (log.isDebugEnabled()) {
          log.debug("Completed task {} in {}ms", task.getName(), executionTimeMs);
        }
        writeTaskStateMetric(
            taskLabels, taskStartTime, taskCompletionTime, getTaskState(), isTaskSkipped);
        TaskInfo.updateInTxn(
            getTaskUUID(),
            tf -> {
              tf.setExecutionTimeMs(executionTimeMs);
              tf.setTotalTimeMs(queuedTimeMs + executionTimeMs);
            });
        task.terminate();
        publishAfterTask(t);
      }
      if (t != null) {
        Throwables.propagate(t);
      }
    }

    public boolean hasTaskCompleted() {
      return getTaskInfo().hasCompleted();
    }

    @Override
    public String toString() {
      TaskInfo taskInfo = getTaskInfo();
      return "task-info {" + taskInfo.toString() + "}, task {" + task.getName() + "}";
    }

    public UUID getTaskUUID() {
      return taskUuid;
    }

    protected void setTaskUUID(UUID taskUuid) {
      this.taskUuid = taskUuid;
    }

    public TaskType getTaskType() {
      return getTaskInfo().getTaskType();
    }

    // This is invoked from tasks to save the updated task details generally in transaction with
    // other DB updates. Avoid using this as there is no transaction.
    public synchronized void setTaskParams(JsonNode taskParams) {
      TaskInfo taskInfo = getTaskInfo();
      taskInfo.setTaskParams(taskParams);
      taskInfo.update();
    }

    protected abstract Map<String, String> getTaskMetricLabels();

    protected abstract boolean isAbortTimeReached(@Nullable Duration graceTime);

    protected abstract TaskExecutionListener getTaskExecutionListener();

    protected abstract UUID getUserTaskUUID();

    protected abstract boolean shouldRun();

    /**
     * Invoked after a run of a task to either report or suppress error. If the throwable is not
     * returned, the error is suppressed.
     */
    protected abstract Throwable handleAfterRun(Throwable t);

    Duration getTimeLimit() {
      return timeLimit;
    }

    void updateScheduledTime() {
      taskScheduledTime = Instant.now();
    }

    TaskInfo.State getTaskState() {
      return getTaskInfo().getTaskState();
    }

    boolean compareAndSetTaskState(Set<TaskInfo.State> expectedStates, TaskInfo.State state) {
      AtomicBoolean affected = new AtomicBoolean();
      TaskInfo.updateInTxn(
          taskUuid,
          tf -> {
            TaskInfo.State currentState = tf.getTaskState();
            if (expectedStates.contains(currentState)) {
              tf.setTaskState(state);
              affected.set(true);
            }
          });
      return affected.get();
    }

    void updateTaskDetailsOnError(TaskInfo.State state, Throwable t) {
      TaskInfo.updateInTxn(taskUuid, tf -> setTaskDetailsOnError(tf, state, t));
    }

    private void setTaskDetailsOnError(TaskInfo taskInfo, TaskInfo.State state, Throwable t) {
      checkNotNull(t);
      checkArgument(
          TaskInfo.ERROR_STATES.contains(state),
          "Task state must be one of " + TaskInfo.ERROR_STATES);
      if (TaskInfo.ERROR_STATES.contains(taskInfo.getTaskState())) {
        log.info(
            "Task {}({}) is already updated with error state {}",
            taskInfo.getTaskType(),
            taskInfo.getUuid(),
            taskInfo.getTaskState());
        return;
      }
      YBAError taskError = null;
      // Method getRedactedParams does not modify the input as it makes a deep-copy.
      String redactedTaskParams = taskInfo.getRedactedParams().toString();
      if (state == TaskInfo.State.Aborted && isShutdown.get()) {
        taskError = new YBAError(Code.PLATFORM_SHUTDOWN, "Platform shutdown");
      } else if (t instanceof TaskExecutionException) {
        TaskExecutionException e = (TaskExecutionException) t;
        taskError = new YBAError(e.getCode(), e.getMessage());
      } else {
        Throwable cause = t;
        // If an exception is eaten up by just wrapping the cause as RuntimeException(e),
        // this can find the actual cause.
        while (StringUtils.isEmpty(cause.getMessage()) && cause.getCause() != null) {
          cause = cause.getCause();
        }
        String errorString =
            String.format(
                "Failed to execute task %s, hit error:\n\n %s.",
                StringUtils.abbreviate(redactedTaskParams, 500),
                StringUtils.abbreviateMiddle(cause.getMessage(), "...", 3000));
        taskError = new YBAError(Code.INTERNAL_ERROR, errorString, cause.getMessage());
      }
      log.error(
          "Failed to execute task type {} UUID {} details {}, hit error.",
          taskInfo.getTaskType(),
          taskInfo.getUuid(),
          redactedTaskParams,
          t);

      if (log.isDebugEnabled()) {
        log.debug("Task creator callstack:\n{}", String.join("\n", creatorCallstack));
      }
      taskInfo.setTaskState(state);
      taskInfo.setTaskError(taskError);
    }

    void publishBeforeTask() {
      TaskExecutionListener taskExecutionListener = getTaskExecutionListener();
      if (taskExecutionListener != null) {
        taskExecutionListener.beforeTask(getTaskInfo());
      }
    }

    void publishAfterTask(Throwable t) {
      TaskExecutionListener taskExecutionListener = getTaskExecutionListener();
      if (taskExecutionListener != null) {
        taskExecutionListener.afterTask(getTaskInfo(), t);
      }
    }
  }

  /** Task runnable */
  public class RunnableTask extends AbstractRunnableTask {
    // Subtask groups to hold subtasks.
    private final Queue<SubTaskGroup> subTaskGroups = new ConcurrentLinkedQueue<>();
    // Latch for timed wait for this task.
    private final CountDownLatch waiterLatch = new CountDownLatch(1);
    // Cache for caching any runtime data when the task is being run.
    private final TaskCache taskCache = new TaskCache();
    // Current execution position of subtasks.
    private int subTaskPosition = 0;
    private final AtomicReference<TaskExecutionListener> taskExecutionListenerRef =
        new AtomicReference<>();
    // Time when the abort is set.
    private final AtomicReference<Supplier<Instant>> abortTimeSupplierRef = new AtomicReference<>();

    RunnableTask(ITask task, UUID taskUuid) {
      super(task);
      super.setTaskUUID(taskUuid);
    }

    /**
     * Sets a TaskExecutionListener instance to get callbacks on before and after each task
     * execution.
     *
     * @param taskExecutionListener the TaskExecutionListener instance.
     */
    public void setTaskExecutionListener(TaskExecutionListener taskExecutionListener) {
      taskExecutionListenerRef.set(taskExecutionListener);
    }

    /**
     * Get the task cache for caching any runtime data.
     *
     * @return the cache instance.
     */
    public TaskCache getTaskCache() {
      return taskCache;
    }

    /** Invoked by the ExecutorService. Do not invoke this directly. */
    @Override
    public void run() {
      UUID taskUUID = getTaskInfo().getUuid();
      try {
        super.run();
      } catch (Throwable t) {
        Throwables.propagate(t);
      } finally {
        // Remove the task.
        runnableTasks.remove(taskUUID);
        // Empty the cache.
        taskCache.clear();
        // Update the customer task to a completed state.
        CustomerTask customerTask = CustomerTask.findByTaskUUID(taskUUID);
        if (customerTask != null && !isShutdown.get()) {
          customerTask.markAsCompleted();
        }

        // In case, it is a scheduled task, update state of the task.
        ScheduleTask scheduleTask = ScheduleTask.fetchByTaskUUID(taskUUID);
        if (scheduleTask != null) {
          scheduleTask.markCompleted();
        }
        // Run a one-off Platform HA sync every time a task finishes.
        replicationManager.oneOffSync();
      }
    }

    /**
     * Clears the already added subtask groups so that they are not run when the RunnableTask is
     * re-run. When runSubTasks() of RunnableTask returns, the current subtasks are discarded. But,
     * if any other operations prior to calling runSubTasks() can fail. So, this method can be used
     * to clean up the previous subtasks.
     */
    public void reset() {
      subTaskGroups.clear();
    }

    @Override
    protected Map<String, String> getTaskMetricLabels() {
      return ImmutableMap.of(
          KnownAlertLabels.PARENT_TASK_TYPE.labelName(),
          "No parent",
          KnownAlertLabels.TASK_TYPE.labelName(),
          getTaskInfo().getTaskType().name());
    }

    private Instant getAbortTime() {
      Supplier<Instant> supplier = abortTimeSupplierRef.get();
      if (supplier == null) {
        return null;
      }
      Instant abortTime = supplier.get();
      if (abortTime == null) {
        throw new IllegalStateException("Abort time must be set by the supplier");
      }
      return abortTime;
    }

    private void setAbortTime(Instant abortTime) {
      abortTimeSupplierRef.set(() -> checkNotNull(abortTime, "Abort time must be set"));
    }

    /**
     * Checks if the future abort time is reached with the additional graceTime if it is provided.
     */
    @Override
    protected boolean isAbortTimeReached(@Nullable Duration graceTime) {
      Instant abortTime = getAbortTime();
      if (abortTime == null) {
        return false;
      }
      long graceMillis = 0L;
      Instant actualAbortTime = abortTime;
      if (graceTime != null && (graceMillis = graceTime.toMillis()) > 0) {
        actualAbortTime = actualAbortTime.plus(graceMillis, ChronoUnit.MILLIS);
      }
      return Instant.now().isAfter(actualAbortTime);
    }

    @Override
    protected TaskExecutionListener getTaskExecutionListener() {
      return taskExecutionListenerRef.get();
    }

    @Override
    protected UUID getUserTaskUUID() {
      return getTaskUUID();
    }

    // Fix the SubTaskGroupType by replacing the default 'Configuring' with the last non-default
    // type if possible. Similar logic is done on serving the API response for task details.
    private void fixSubTaskGroupType() {
      SubTaskGroupType lastNonDefaultGroupType = SubTaskGroupType.Configuring;
      for (SubTaskGroup subTaskGroup : subTaskGroups) {
        if (subTaskGroup.getSubTaskCount() == 0) {
          continue;
        }
        if (subTaskGroup.getSubTaskGroupType() == SubTaskGroupType.Configuring) {
          log.warn(
              "SubTaskGroupType is set to default '{}' for {}",
              SubTaskGroupType.Configuring,
              subTaskGroup.getName());
        } else {
          // Update it to the non default SubTaskGroupType.
          lastNonDefaultGroupType = subTaskGroup.getSubTaskGroupType();
        }
        if (lastNonDefaultGroupType != subTaskGroup.getSubTaskGroupType()) {
          // Current SubTaskGroupType is the default type which needs to be overridden.
          log.info("Using the last SubTaskGroupType {}", lastNonDefaultGroupType);
          subTaskGroup.setSubTaskGroupType(lastNonDefaultGroupType);
        }
      }
    }

    /**
     * Adds the SubTaskGroup instance containing the subtasks which are to be executed concurrently.
     *
     * @param subTaskGroup the subtask group of subtasks to be executed concurrently.
     */
    public void addSubTaskGroup(SubTaskGroup subTaskGroup) {
      log.info("Adding SubTaskGroup #{}: {}", subTaskGroup.getSubTaskCount(), subTaskGroup.name);
      if (subTaskGroup.getSubTaskCount() == 0) {
        // Allowing to add this just messes up the positions.
        log.info("Ignoring subtask SubTaskGroup {} as it is empty", subTaskGroup.name);
        return;
      }
      subTaskGroup.setRunnableTaskContext(this, subTaskPosition);
      subTaskGroups.add(subTaskGroup);
      subTaskPosition++;
    }

    /**
     * Starts execution of the subtasks in the groups for this runnable task and waits for
     * completion. This method is invoked inside the run() method of the task e.g CreateUniverse to
     * start execution of the subtasks.
     */
    public void runSubTasks() {
      runSubTasks(false);
    }

    /**
     * Equivalent to runSubTasks() method, additional abortOnFailure to abort peer subtasks if one
     * fails.
     *
     * @param abortOnFailure boolean whether to abort peer subtasks on failure of one subtask.
     */
    public void runSubTasks(boolean abortOnFailure) {
      Throwable anyThrowable = null;
      try {
        fixSubTaskGroupType();
        for (SubTaskGroup subTaskGroup : subTaskGroups) {
          if (subTaskGroup.getSubTaskCount() == 0) {
            continue;
          }
          ExecutorService executorService = subTaskGroup.getSubTaskExecutorService();
          if (executorService == null) {
            executorService = executorServiceProvider.getExecutorServiceFor(getTaskType());
            subTaskGroup.setSubTaskExecutor(executorService);
          }
          checkNotNull(executorService, "ExecutorService must be set");
          try {
            try {
              // This can throw rare exception on task submission error.
              // Submission error must not be suppressed on ignore error.
              subTaskGroup.submitSubTasks();
            } catch (RejectedExecutionException e) {
              throw e;
            } catch (RuntimeException e) {
              throw new RejectedExecutionException(e);
            } finally {
              // TODO Does it make sense to abort the task?
              // There can be conflicts between aborted and failed task states.
              // Wait for already submitted subtasks.
              subTaskGroup.waitForSubTasks(abortOnFailure);
            }
            // Invoke post-run method after successful completion of all subtasks.
            subTaskGroup.handleAfterGroupRun();
          } catch (CancellationException | RejectedExecutionException e) {
            throw new CancellationException(subTaskGroup.toString() + " is cancelled.");
          } catch (Throwable t) {
            if (subTaskGroup.ignoreErrors) {
              log.error("Ignoring error for " + subTaskGroup, t);
            } else if (t instanceof Error) {
              // Error is propagated as it is.
              throw (Error) t;
            } else {
              // Postpone throwing this error later when all the subgroups are done.
              throw new RuntimeException(subTaskGroup + " failed.", t);
            }
            anyThrowable = t;
          }
        }
      } finally {
        // Clear the subtasks so that new subtasks can be run from the clean state.
        subTaskGroups.clear();
      }
      if (anyThrowable != null) {
        if (anyThrowable instanceof Error) {
          // Error is propagated as it is.
          throw (Error) anyThrowable;
        }
        throw new RuntimeException("One or more SubTaskGroups failed while running.", anyThrowable);
      }
    }

    /**
     * Abort-aware wait function makes the current thread to wait until the timeout or the abort
     * signal is received. It can be a replacement for Thread.sleep in subtasks.
     *
     * @param waitTime the maximum time to wait.
     */
    public void waitFor(Duration waitTime) {
      checkNotNull(waitTime);
      try {
        if (waiterLatch.await(waitTime.toMillis(), TimeUnit.MILLISECONDS)) {
          // Count reached zero first, another thread must have decreased it.
          throw new CancellationException(this + " is aborted while waiting.");
        }
      } catch (InterruptedException e) {
        throw new CancellationException(e.getMessage());
      }
    }

    /** Cancel the waiter latch if the task is aborted. */
    @VisibleForTesting
    void cancelWaiterIfAborted() {
      if (getAbortTime() != null) {
        waiterLatch.countDown();
      }
    }

    // Restricted access to package level for internal use.
    boolean isRunning() {
      return runnableTasks.containsKey(getTaskUUID());
    }

    // Restricted access to package level for internal use.
    // Sets a supplier for abort time on request.
    void setAbortTimeSupplier(Supplier<Instant> abortTimeSupplier) {
      abortTimeSupplierRef.compareAndSet(null, abortTimeSupplier);
    }

    // Restricted access to package level for internal use.
    void abort(@Nullable Duration delay) {
      Instant abortTime = Instant.now();
      if (delay != null && delay.toMillis() > 0) {
        abortTime = abortTime.plus(delay.toMillis(), ChronoUnit.MILLIS);
      }
      Instant currentAbortTime = getAbortTime();
      // Signal abort to the task.
      if (currentAbortTime == null || currentAbortTime.isAfter(abortTime)) {
        log.info("Aborting task {} in {} secs", getTaskUUID(), abortTime);
        setAbortTime(abortTime);
      }
    }

    @Override
    protected boolean shouldRun() {
      return true;
    }

    @Override
    protected Throwable handleAfterRun(Throwable t) {
      return t;
    }
  }

  /** Runnable task for subtasks in a task. */
  public class RunnableSubTask extends AbstractRunnableTask {
    private final SubTaskGroup subTaskGroup;
    private RunnableTask parentRunnableTask;

    RunnableSubTask(SubTaskGroup subTaskGroup, ITask task) {
      super(task);
      this.subTaskGroup = subTaskGroup;
    }

    private void executeWith(ExecutorService executorService) {
      try {
        updateScheduledTime();
        future = executorService.submit(this);
      } catch (RuntimeException e) {
        // Subtask submission failed.
        updateTaskDetailsOnError(TaskInfo.State.Failure, e);
        publishAfterTask(e);
        throw e;
      }
    }

    @Override
    public void run() {
      int currentAttempt = 0;
      int retryLimit = getTask().getRetryLimit();

      while (currentAttempt < retryLimit) {
        try {
          super.run();
          break;
        } catch (Throwable t) {
          if (t instanceof Error) {
            // Error is propagated as it is.
            throw (Error) t;
          }
          if ((currentAttempt == retryLimit - 1)
              || ExceptionUtils.hasCause(t, CancellationException.class)) {
            throw t;
          }

          String redactedParams =
              RedactingService.filterSecretFields(getTask().getTaskParams(), RedactionTarget.LOGS)
                  .toString();
          log.warn(
              "Task {} with params {} attempt {} has failed",
              getTask().getName(),
              redactedParams,
              currentAttempt);
          if (!getTask().onFailure(getTaskInfo(), t)) {
            throw t;
          }
        }

        currentAttempt++;
      }
    }

    /** Return the subtask group to which this runnable task belongs. */
    public SubTaskGroup getSubTaskGroup() {
      return subTaskGroup;
    }

    @Override
    protected Map<String, String> getTaskMetricLabels() {
      return ImmutableMap.of(
          KnownAlertLabels.PARENT_TASK_TYPE.labelName(),
          parentRunnableTask.getTaskType().name(),
          KnownAlertLabels.TASK_TYPE.labelName(),
          getTaskInfo().getTaskType().name(),
          TASK_EXECUTION_SKIPPED_LABEL,
          String.valueOf(false));
    }

    @Override
    protected boolean isAbortTimeReached(@Nullable Duration graceTime) {
      return parentRunnableTask == null ? false : parentRunnableTask.isAbortTimeReached(graceTime);
    }

    @Override
    protected TaskExecutionListener getTaskExecutionListener() {
      return parentRunnableTask == null ? null : parentRunnableTask.getTaskExecutionListener();
    }

    @Override
    protected UUID getUserTaskUUID() {
      return parentRunnableTask == null ? null : parentRunnableTask.getUserTaskUUID();
    }

    // SubTaskGroupType can be set before or after the group is added to the parent task.
    void setSubTaskGroupType(SubTaskGroupType subTaskGroupType) {
      UUID taskUuid = getTaskUUID();
      if (taskUuid != null) {
        TaskInfo.maybeGet(taskUuid)
            .ifPresent(
                tf -> {
                  // Update task info if the group with subtasks is alreay added.
                  tf.setSubTaskGroupType(subTaskGroupType);
                  tf.update();
                });
      }
    }

    private void setRunnableTaskContext(RunnableTask parentRunnableTask, int position) {
      this.parentRunnableTask = parentRunnableTask;
      TaskInfo taskInfo = createTaskInfo(getTask(), null, null);
      taskInfo.setSubTaskGroupType(getSubTaskGroup().getSubTaskGroupType());
      taskInfo.setParentUuid(parentRunnableTask.getTaskUUID());
      taskInfo.setPosition(position);
      taskInfo.save();
      setTaskUUID(taskInfo.getUuid());
      getTask().setTaskUUID(taskInfo.getUuid());
      getTask().setUserTaskUUID(parentRunnableTask.getTaskUUID());
    }

    @Override
    protected boolean shouldRun() {
      Predicate<ITask> predicate = subTaskGroup.shouldRunPredicateRef.get();
      return predicate == null ? true : predicate.test(getTask());
    }

    @Override
    protected Throwable handleAfterRun(Throwable t) {
      // Delegate it to the subtask group.
      return getSubTaskGroup().handleAfterTaskRun(this, t);
    }
  }
}
