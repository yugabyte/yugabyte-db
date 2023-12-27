// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import static com.google.common.base.Preconditions.checkArgument;
import static com.google.common.base.Preconditions.checkNotNull;
import static com.yugabyte.yw.models.helpers.CommonUtils.getDurationSeconds;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.api.client.util.Throwables;
import com.google.common.annotations.VisibleForTesting;
import com.google.common.collect.Sets;
import com.google.inject.Provider;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.common.DrainableMap;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.RedactingService;
import com.yugabyte.yw.common.RedactingService.RedactionTarget;
import com.yugabyte.yw.common.ShutdownHookHandler;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.ha.PlatformReplicationManager;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.ScheduleTask;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.TaskInfo.State;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import com.yugabyte.yw.models.helpers.TaskType;
import io.prometheus.client.CollectorRegistry;
import io.prometheus.client.Summary;
import java.time.Duration;
import java.time.Instant;
import java.util.Collections;
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
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;
import java.util.stream.Collectors;
import java.util.stream.IntStream;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import org.apache.commons.lang3.exception.ExceptionUtils;

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

  // Default wait timeout for subtasks to complete since the abort call.
  private final Duration defaultAbortTaskTimeout = Duration.ofSeconds(60);

  // ExecutorService provider for subtasks if explicit ExecutorService
  // is set for the subtasks in a task.
  private final ExecutorServiceProvider executorServiceProvider;

  // A map from task UUID to its RunnableTask while it is running.
  private final DrainableMap<UUID, RunnableTask> runnableTasks = new DrainableMap<>();

  // A utility for Platform HA.
  private final PlatformReplicationManager replicationManager;
  private final Map<Class<? extends ITask>, TaskType> inverseTaskTypeMap;

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
          KnownAlertLabels.TASK_TYPE.labelName());

  private static final Summary COMMISSIONER_TASK_EXECUTION_SEC =
      buildSummary(
          COMMISSIONER_TASK_EXECUTION_SEC_METRIC,
          "Duration of task execution",
          KnownAlertLabels.TASK_TYPE.labelName(),
          KnownAlertLabels.RESULT.labelName());

  private static Summary buildSummary(String name, String description, String... labelNames) {
    return Summary.build(name, description)
        .quantile(0.5, 0.05)
        .quantile(0.9, 0.01)
        .maxAgeSeconds(TimeUnit.HOURS.toSeconds(1))
        .labelNames(labelNames)
        .register(CollectorRegistry.defaultRegistry);
  }

  // This writes the waiting time metric.
  private static void writeTaskWaitMetric(
      TaskType taskType, Instant scheduledTime, Instant startTime) {
    COMMISSIONER_TASK_WAITING_SEC
        .labels(taskType.name())
        .observe(getDurationSeconds(scheduledTime, startTime));
  }

  // This writes the execution time metric.
  private static void writeTaskStateMetric(
      TaskType taskType, Instant startTime, Instant endTime, State state) {
    COMMISSIONER_TASK_EXECUTION_SEC
        .labels(taskType.name(), state.name())
        .observe(getDurationSeconds(startTime, endTime));
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
      Map<Class<? extends ITask>, TaskType> inverseTaskTypeMap) {
    this.executorServiceProvider = executorServiceProvider;
    this.replicationManager = replicationManager;
    this.taskOwner = Util.getHostname();
    this.skipSubTaskAbortableCheck = true;
    shutdownHookHandler.addShutdownHook(
        TaskExecutor.this,
        (taskExecutor) -> taskExecutor.shutdown(Duration.ofMinutes(5)),
        100 /* weight */);
    this.taskTypeMap = taskTypeMap;
    this.inverseTaskTypeMap = inverseTaskTypeMap;
  }

  // Shuts down the task executor.
  // It assumes that the executor services will
  // also be shutdown gracefully.
  public boolean shutdown(Duration timeout) {
    if (isShutdown.compareAndSet(false, true)) {
      log.info("TaskExecutor is shutting down");
      runnableTasks.sealMap();
      Instant abortTime = Instant.now();
      synchronized (runnableTasks) {
        runnableTasks.forEach(
            (uuid, runnable) -> {
              runnable.setAbortTime(abortTime);
              runnable.cancelWaiterIfAborted();
            });
      }
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
      throw new IllegalStateException("TaskExecutor is shutting down");
    }
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
   * @param taskType the task type.
   * @param taskParams the task parameters.
   */
  public RunnableTask createRunnableTask(TaskType taskType, ITaskParams taskParams) {
    checkNotNull(taskType, "Task type must be set");
    checkNotNull(taskParams, "Task params must be set");
    ITask task = taskTypeMap.get(taskType).get();
    task.initialize(taskParams);
    return createRunnableTask(task);
  }

  /**
   * Creates a RunnableTask instance for the given task.
   *
   * @param task the task.
   */
  public RunnableTask createRunnableTask(ITask task) {
    checkNotNull(task, "Task must be set");
    try {
      task.validateParams(task.isFirstTry());
    } catch (PlatformServiceException e) {
      log.error("Params validation failed for task " + task, e);
      throw e;
    } catch (Exception e) {
      log.error("Params validation failed for task " + task, e);
      throw new PlatformServiceException(BAD_REQUEST, e.getMessage());
    }
    TaskInfo taskInfo = createTaskInfo(task);
    taskInfo.setPosition(-1);
    taskInfo.save();
    return new RunnableTask(task, taskInfo);
  }

  /**
   * Submits a RunnableTask to this TaskExecutor with the given ExecutorService for execution. If
   * the task has subtasks, they are submitted to this RunnableTask in its run() method.
   *
   * @param runnableTask the RunnableTask instance.
   * @param taskExecutorService the ExecutorService for this task.
   */
  public UUID submit(RunnableTask runnableTask, ExecutorService taskExecutorService) {
    checkTaskExecutorState();
    checkNotNull(runnableTask, "Task runnable must not be null");
    checkNotNull(taskExecutorService, "Task executor service must not be null");
    UUID taskUUID = runnableTask.getTaskUUID();
    runnableTasks.put(taskUUID, runnableTask);
    try {
      runnableTask.updateScheduledTime();
      runnableTask.future = taskExecutorService.submit(runnableTask);
    } catch (Exception e) {
      // Update task state on submission failure.
      runnableTasks.remove(taskUUID);
      runnableTask.updateTaskDetailsOnError(TaskInfo.State.Failure, e);
    }
    return taskUUID;
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
    } catch (Exception e) {
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
    // Signal abort to the task.
    if (runnableTask.getAbortTime() == null) {
      // This is not atomic but it is ok.
      runnableTask.setAbortTime(Instant.now());
      runnableTask.cancelWaiterIfAborted();
    }
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
    return createSubTaskGroup(name, SubTaskGroupType.Invalid, false);
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
    SubTaskGroup subTaskGroup = createSubTaskGroup(name, SubTaskGroupType.Invalid, ignoreErrors);
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
  TaskInfo createTaskInfo(ITask task) {
    TaskType taskType = getTaskType(task.getClass());
    // Create a new task info object.
    TaskInfo taskInfo = new TaskInfo(taskType);
    // Set the task details.
    taskInfo.setDetails(
        RedactingService.filterSecretFields(task.getTaskDetails(), RedactionTarget.APIS));
    // Set the owner info.
    taskInfo.setOwner(taskOwner);
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

  // Optionally returns the task runnable with the given task UUID.
  private Optional<RunnableTask> maybeGetRunnableTask(UUID taskUUID) {
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
    private final Set<RunnableSubTask> subTasks =
        Collections.newSetFromMap(new ConcurrentHashMap<>());
    private final String name;
    private final boolean ignoreErrors;
    private final AtomicInteger numTasksCompleted;

    // Parent task runnable to which this group belongs.
    private volatile RunnableTask runnableTask;
    // Group position which is same as that of the subtasks.
    private int position;
    // Optional executor service for the subtasks.
    private ExecutorService executorService;
    private SubTaskGroupType subTaskGroupType = SubTaskGroupType.Invalid;

    // It is instantiated internally.
    private SubTaskGroup(String name, SubTaskGroupType subTaskGroupType, boolean ignoreErrors) {
      this.name = name;
      this.subTaskGroupType = subTaskGroupType;
      this.ignoreErrors = ignoreErrors;
      this.numTasksCompleted = new AtomicInteger();
    }

    /**
     * Adds a subtask to this SubTaskGroup. It adds the subtask in memory and is not run yet. When
     * the this SubTaskGroup is added to the RunnableTask, the subtasks are persisted.
     *
     * @param subTask the subtask.
     */
    public void addSubTask(ITask subTask) {
      checkNotNull(subTask, "Subtask must be non-null");
      int subTaskCount = getSubTaskCount();
      log.info("Adding task #{}: {}", subTaskCount, subTask.getName());
      if (log.isDebugEnabled()) {
        JsonNode redactedTask =
            RedactingService.filterSecretFields(subTask.getTaskDetails(), RedactionTarget.LOGS);
        log.debug(
            "Details for task #{}: {} details= {}", subTaskCount, subTask.getName(), redactedTask);
      }
      TaskInfo taskInfo = createTaskInfo(subTask);
      taskInfo.setSubTaskGroupType(subTaskGroupType);
      subTasks.add(new RunnableSubTask(subTask, taskInfo));
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
      numTasksCompleted.incrementAndGet();
      runnableSubTask.publishAfterTask(throwable);
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
            if (abortOnFailure) {
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
            Instant abortTime = runnableTask.getAbortTime();
            // If the subtask execution takes long, it is interrupted.
            if (!timeout.isZero() && elapsed.compareTo(timeout) > 0) {
              anyEx = e;
              future.cancel(true);
              // Report failure to the parent task.
              // Update the subtask state to aborted if the execution timed out.
              runnableSubTask.updateTaskDetailsOnError(TaskInfo.State.Aborted, e);
              removeCompletedSubTask(iter, runnableSubTask, e);
            } else if (abortTime != null
                && Duration.between(abortTime, Instant.now()).compareTo(defaultAbortTaskTimeout) > 0
                && (skipSubTaskAbortableCheck
                    || isTaskAbortable(runnableSubTask.getTask().getClass()))) {
              future.cancel(true);
              // Report aborted to the parent task.
              // Update the subtask state to aborted if the execution timed out.
              Throwable thisEx = new CancellationException(e.getMessage());
              anyEx = (anyEx != null) ? anyEx : thisEx;
              runnableSubTask.updateTaskDetailsOnError(TaskInfo.State.Aborted, thisEx);
              removeCompletedSubTask(iter, runnableSubTask, anyEx);
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
          } catch (Exception e) {
            anyEx = e;
            runnableSubTask.updateTaskDetailsOnError(TaskInfo.State.Failure, e);
            removeCompletedSubTask(iter, runnableSubTask, e);
          }
        }
      }
      Duration elapsed = Duration.between(waitStartTime, Instant.now());
      log.debug("{}: wait completed in {}ms", title(), elapsed.toMillis());
      if (anyEx != null) {
        Throwables.propagate(anyEx);
      }
    }

    /**
     * Sets the SubTaskGroupType for this SubTaskGroup.
     *
     * @param subTaskGroupType the SubTaskGroupType.
     */
    public void setSubTaskGroupType(SubTaskGroupType subTaskGroupType) {
      if (subTaskGroupType == null) {
        return;
      }
      log.info("Setting subtask({}) group type to {}", name, subTaskGroupType);
      this.subTaskGroupType = subTaskGroupType;
      for (RunnableSubTask runnable : subTasks) {
        runnable.setSubTaskGroupType(subTaskGroupType);
      }
    }

    public int getSubTaskCount() {
      return subTasks.size();
    }

    public int getTasksCompletedCount() {
      return numTasksCompleted.get();
    }

    private String title() {
      return String.format(
          "SubTaskGroup %s of type %s at position %d", name, subTaskGroupType.name(), position);
    }

    @Override
    public String toString() {
      return String.format(
          "%s: completed %d out of %d tasks", title(), getTasksCompletedCount(), getSubTaskCount());
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
    private final TaskInfo taskInfo;
    // Timeout limit for this task.
    private final Duration timeLimit;
    private final String[] creatorCallstack;

    private Instant taskScheduledTime;
    private Instant taskStartTime;
    private Instant taskCompletionTime;

    // Future of the task that is set after it is submitted to the ExecutorService.
    Future<?> future = null;

    protected AbstractRunnableTask(ITask task, TaskInfo taskInfo) {
      this.task = task;
      this.taskInfo = taskInfo;
      this.taskScheduledTime = Instant.now();

      Duration duration = Duration.ZERO;
      JsonNode jsonNode = taskInfo.getDetails();
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
      return taskInfo;
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
      try {
        if (log.isDebugEnabled()) {
          log.debug(
              "Task {} waited for {}s",
              task.getName(),
              getDurationSeconds(taskScheduledTime, taskStartTime));
        }
        writeTaskWaitMetric(getTaskType(), taskScheduledTime, taskStartTime);
        publishBeforeTask();
        if (getAbortTime() != null) {
          throw new CancellationException("Task " + task.getName() + " is aborted");
        }
        setTaskState(TaskInfo.State.Running);
        log.debug("Invoking run() of task {}", task.getName());
        task.setTaskUUID(getTaskUUID());
        task.run();
        setTaskState(TaskInfo.State.Success);
      } catch (CancellationException e) {
        t = e;
        updateTaskDetailsOnError(TaskInfo.State.Aborted, e);
        onCancelled();
        throw e;
      } catch (Exception e) {
        if (ExceptionUtils.hasCause(e, CancellationException.class)) {
          t = new CancellationException(e.getMessage());
          updateTaskDetailsOnError(TaskInfo.State.Aborted, e);
          onCancelled();
        } else {
          t = e;
          updateTaskDetailsOnError(TaskInfo.State.Failure, e);
        }
        Throwables.propagate(t);
      } finally {
        taskCompletionTime = Instant.now();
        if (log.isDebugEnabled()) {
          log.debug(
              "Completed task {} in {}s",
              task.getName(),
              getDurationSeconds(taskStartTime, taskCompletionTime));
        }
        writeTaskStateMetric(getTaskType(), taskStartTime, taskCompletionTime, getTaskState());
        task.terminate();
        publishAfterTask(t);
      }
    }

    public synchronized boolean isTaskRunning() {
      return taskInfo.getTaskState() == TaskInfo.State.Running;
    }

    public synchronized boolean hasTaskCompleted() {
      return taskInfo.hasCompleted();
    }

    @Override
    public String toString() {
      return "task-info {" + taskInfo.toString() + "}, task {" + task.getName() + "}";
    }

    public UUID getTaskUUID() {
      return taskInfo.getTaskUUID();
    }

    // This is invoked from tasks to save the updated task details generally in transaction with
    // other DB updates.
    public synchronized void setTaskDetails(JsonNode taskDetails) {
      taskInfo.refresh();
      taskInfo.setDetails(taskDetails);
      taskInfo.update();
    }

    protected abstract Instant getAbortTime();

    protected abstract TaskExecutionListener getTaskExecutionListener();

    protected abstract UUID getUserTaskUUID();

    Duration getTimeLimit() {
      return timeLimit;
    }

    void updateScheduledTime() {
      taskScheduledTime = Instant.now();
    }

    TaskType getTaskType() {
      return taskInfo.getTaskType();
    }

    synchronized void setPosition(int position) {
      taskInfo.setPosition(position);
    }

    synchronized TaskInfo.State getTaskState() {
      return taskInfo.getTaskState();
    }

    synchronized void setTaskState(TaskInfo.State state) {
      taskInfo.setTaskState(state);
      taskInfo.update();
    }

    synchronized boolean compareAndSetTaskState(TaskInfo.State expected, TaskInfo.State state) {
      return compareAndSetTaskState(Sets.immutableEnumSet(expected), state);
    }

    synchronized boolean compareAndSetTaskState(
        Set<TaskInfo.State> expectedStates, TaskInfo.State state) {
      TaskInfo.State currentState = taskInfo.getTaskState();
      if (expectedStates.contains(currentState)) {
        setTaskState(state);
        return true;
      }
      return false;
    }

    synchronized void updateTaskDetailsOnError(TaskInfo.State state, Throwable t) {
      checkNotNull(t);
      checkArgument(
          TaskInfo.ERROR_STATES.contains(state),
          "Task state must be one of " + TaskInfo.ERROR_STATES);
      taskInfo.refresh();
      ObjectNode taskDetails = taskInfo.getDetails().deepCopy();
      // Method maskConfig does not modify the input as it makes a deep-copy.
      String maskedTaskDetails = CommonUtils.maskConfig(taskDetails).toString();
      String errorString;
      if (state == TaskInfo.State.Aborted && isShutdown.get()) {
        errorString = "Platform shutdown";
      } else {
        Throwable cause = t;
        // If an exception is eaten up by just wrapping the cause as RuntimeException(e),
        // this can find the actual cause.
        while (StringUtils.isEmpty(cause.getMessage()) && cause.getCause() != null) {
          cause = cause.getCause();
        }
        errorString =
            String.format(
                "Failed to execute task %s, git error:\n\n %s.",
                StringUtils.abbreviate(maskedTaskDetails, 500),
                StringUtils.abbreviateMiddle(cause.getMessage(), "...", 3000));
      }
      log.error(
          "Failed to execute task type {} UUID {} details {}, hit error.",
          taskInfo.getTaskType(),
          taskInfo.getTaskUUID(),
          maskedTaskDetails,
          t);

      if (log.isDebugEnabled()) {
        log.debug("Task creator callstack:\n{}", String.join("\n", creatorCallstack));
      }

      taskDetails.put("errorString", errorString);
      taskInfo.setTaskState(state);
      taskInfo.setDetails(taskDetails);
      taskInfo.update();
    }

    void publishBeforeTask() {
      TaskExecutionListener taskExecutionListener = getTaskExecutionListener();
      if (taskExecutionListener != null) {
        taskExecutionListener.beforeTask(taskInfo);
      }
    }

    void publishAfterTask(Throwable t) {
      TaskExecutionListener taskExecutionListener = getTaskExecutionListener();
      if (taskExecutionListener != null) {
        taskExecutionListener.afterTask(taskInfo, t);
      }
    }

    void onCancelled() {
      try {
        task.onCancelled(taskInfo);
      } catch (Exception e) {
        // Do not propagate the exception as the task is already cancelled.
        log.warn("Error occurred in onCancelled", e);
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
    private volatile Instant abortTime;

    RunnableTask(ITask task, TaskInfo taskInfo) {
      super(task, taskInfo);
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
      UUID taskUUID = getTaskInfo().getTaskUUID();
      try {
        getTask().setUserTaskUUID(taskUUID);
        super.run();
      } catch (Exception e) {
        Throwables.propagate(e);
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
    protected Instant getAbortTime() {
      return abortTime;
    }

    private void setAbortTime(Instant abortTime) {
      this.abortTime = abortTime;
    }

    @Override
    protected TaskExecutionListener getTaskExecutionListener() {
      return taskExecutionListenerRef.get();
    }

    @Override
    protected UUID getUserTaskUUID() {
      return getTaskUUID();
    }

    public synchronized void doHeartbeat() {
      log.trace("Heartbeating task {}", getTaskUUID());
      TaskInfo taskInfo = TaskInfo.getOrBadRequest(getTaskUUID());
      taskInfo.markAsDirty();
      taskInfo.update();
    }

    /**
     * Adds the SubTaskGroup instance containing the subtasks which are to be executed concurrently.
     *
     * @param subTaskGroup the subtask group of subtasks to be executed concurrently.
     */
    public void addSubTaskGroup(SubTaskGroup subTaskGroup) {
      log.info("Adding SubTaskGroup #{}: {}", subTaskGroups.size(), subTaskGroup.name);
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
      RuntimeException anyRe = null;
      try {
        for (SubTaskGroup subTaskGroup : subTaskGroups) {
          if (subTaskGroup.getSubTaskCount() == 0) {
            // TODO Some groups are added without any subtasks in a task like
            // CreateKubernetesUniverse.
            // It needs to be fixed first before this can prevent empty groups from getting added.
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
              subTaskGroup.submitSubTasks();
            } finally {
              // TODO Does it make sense to abort the task?
              // There can be conflicts between aborted and failed task states.
              // Wait for already submitted subtasks.
              subTaskGroup.waitForSubTasks(abortOnFailure);
            }
          } catch (CancellationException e) {
            throw new CancellationException(subTaskGroup.toString() + " is cancelled.");
          } catch (RuntimeException e) {
            if (subTaskGroup.ignoreErrors) {
              log.error("Ignoring error for " + subTaskGroup, e);
            } else {
              // Postpone throwing this error later when all the subgroups are done.
              throw new RuntimeException(subTaskGroup + " failed.", e);
            }
            anyRe = e;
          }
        }
      } finally {
        // Clear the subtasks so that new subtasks can be run from the clean state.
        subTaskGroups.clear();
      }
      if (anyRe != null) {
        throw new RuntimeException("One or more SubTaskGroups failed while running.", anyRe);
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
  }

  /** Runnable task for subtasks in a task. */
  public class RunnableSubTask extends AbstractRunnableTask {
    private RunnableTask parentRunnableTask;

    RunnableSubTask(ITask task, TaskInfo taskInfo) {
      super(task, taskInfo);
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
      // Sets the top-level user task UUID.
      getTask().setUserTaskUUID(getUserTaskUUID());
      int currentAttempt = 0;
      int retryLimit = getTask().getRetryLimit();

      while (currentAttempt < retryLimit) {
        try {
          super.run();
          break;
        } catch (Exception e) {
          if ((currentAttempt == retryLimit - 1)
              || ExceptionUtils.hasCause(e, CancellationException.class)) {
            throw e;
          }

          log.warn("Task {} attempt {} has failed", getTask(), currentAttempt);
          getTask().onFailure(getTaskInfo(), e);
        }
        currentAttempt++;
      }
    }

    @Override
    protected synchronized Instant getAbortTime() {
      return parentRunnableTask == null ? null : parentRunnableTask.getAbortTime();
    }

    @Override
    protected synchronized TaskExecutionListener getTaskExecutionListener() {
      return parentRunnableTask == null ? null : parentRunnableTask.getTaskExecutionListener();
    }

    @Override
    protected synchronized UUID getUserTaskUUID() {
      return parentRunnableTask == null ? null : parentRunnableTask.getUserTaskUUID();
    }

    public synchronized void setSubTaskGroupType(SubTaskGroupType subTaskGroupType) {
      if (getTaskInfo().getSubTaskGroupType() != subTaskGroupType) {
        getTaskInfo().setSubTaskGroupType(subTaskGroupType);
        getTaskInfo().save();
      }
    }

    private synchronized void setRunnableTaskContext(
        RunnableTask parentRunnableTask, int position) {
      this.parentRunnableTask = parentRunnableTask;
      getTaskInfo().setParentUuid(parentRunnableTask.getTaskUUID());
      getTaskInfo().setPosition(position);
      getTaskInfo().save();
    }
  }
}
