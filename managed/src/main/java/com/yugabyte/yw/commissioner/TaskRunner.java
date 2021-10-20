// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.commissioner;

import static com.yugabyte.yw.models.helpers.CommonUtils.getDurationSeconds;

import com.yugabyte.yw.common.ha.PlatformReplicationManager;
import com.yugabyte.yw.common.password.RedactingService;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.ScheduleTask;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.TaskInfo.State;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import com.yugabyte.yw.models.helpers.TaskType;
import io.prometheus.client.CollectorRegistry;
import io.prometheus.client.Summary;
import java.net.InetAddress;
import java.net.UnknownHostException;
import java.util.Collections;
import java.util.Date;
import java.util.HashMap;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.TimeUnit;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.api.Play;

/**
 * This class is responsible for creating and running a task. It provides all the common
 * infrastructure across the different types of tasks. It creates and keeps an instance of the ITask
 * object that actually performs the work specific to the current task type.
 */
public class TaskRunner implements Runnable {

  // Metric names
  private static final String COMMISSIONER_TASK_WAITING_SEC_METRIC =
      "ybp_commissioner_task_waiting_sec";
  private static final String COMMISSIONER_TASK_EXECUTION_SEC_METRIC =
      "ybp_commissioner_task_execution_sec";

  // Metrics
  public static final Summary COMMISSIONER_TASK_WAITING_SEC =
      buildSummary(
          COMMISSIONER_TASK_WAITING_SEC_METRIC,
          "Duration between task creation and execution",
          KnownAlertLabels.TASK_TYPE.labelName());
  public static final Summary COMMISSIONER_TASK_EXECUTION_SEC =
      buildSummary(
          COMMISSIONER_TASK_EXECUTION_SEC_METRIC,
          "Duration of task execution",
          KnownAlertLabels.TASK_TYPE.labelName(),
          KnownAlertLabels.RESULT.labelName());

  public static final Logger LOG = LoggerFactory.getLogger(TaskRunner.class);

  // This is a map from the task types to the classes so that we can instantiate the task.
  private static Map<TaskType, Class<? extends ITask>> taskTypeToTaskClassMap;

  // The data underlying the task.
  private TaskInfo taskInfo;

  // The task object that will run the current task.
  private ITask task;

  // A utility for Platform HA.
  private final PlatformReplicationManager replicationManager;

  static {
    // Initialize the map which holds the task types to their task class.
    Map<TaskType, Class<? extends ITask>> typeMap = new HashMap<TaskType, Class<? extends ITask>>();

    for (TaskType taskType : TaskType.filteredValues()) {
      String className = "com.yugabyte.yw.commissioner.tasks." + taskType.toString();
      Class<? extends ITask> taskClass;
      try {
        taskClass = Class.forName(className).asSubclass(ITask.class);
        typeMap.put(taskType, taskClass);
        LOG.debug("Found task: " + className);
      } catch (ClassNotFoundException e) {
        LOG.error("Could not find task for task type " + taskType, e);
      }
    }
    taskTypeToTaskClassMap = Collections.unmodifiableMap(typeMap);
    LOG.debug("Done loading tasks.");
  }

  static Summary buildSummary(String name, String description, String... labelNames) {
    return Summary.build(name, description)
        .quantile(0.5, 0.05)
        .quantile(0.9, 0.01)
        .maxAgeSeconds(TimeUnit.HOURS.toSeconds(1))
        .labelNames(labelNames)
        .register(CollectorRegistry.defaultRegistry);
  }

  /**
   * Creates the task runner along with the task object and persists the task info info.
   *
   * @param taskType : the task type
   * @param claimTask : if true, adds this process as the owner of the task being created
   * @return the TaskRunner object on which run can be called.
   * @throws InstantiationException
   * @throws IllegalAccessException
   */
  public static TaskRunner createTask(TaskType taskType, ITaskParams taskParams, boolean claimTask)
      throws InstantiationException, IllegalAccessException {

    // Create the task runner object.
    TaskRunner taskRunner = new TaskRunner(taskType, taskParams);

    // Persist the task in the queue.
    taskRunner.save();

    return taskRunner;
  }

  private TaskRunner(TaskType taskType, ITaskParams taskParams)
      throws InstantiationException, IllegalAccessException {

    // Create an instance of the task.
    task = Play.current().injector().instanceOf(taskTypeToTaskClassMap.get(taskType));
    // Init the task.
    task.initialize(taskParams);
    // Create a new task info object.
    taskInfo = new TaskInfo(taskType);
    // Set the task details.
    taskInfo.setTaskDetails(RedactingService.filterSecretFields(task.getTaskDetails()));
    // Set the owner info.
    String hostname = "";
    try {
      hostname = InetAddress.getLocalHost().getHostName();
    } catch (UnknownHostException e) {
      LOG.error("Could not determine the hostname", e);
    }
    taskInfo.setOwner(hostname);
    replicationManager = Play.current().injector().instanceOf(PlatformReplicationManager.class);
  }

  public UUID getTaskUUID() {
    return taskInfo.getTaskUUID();
  }

  /** Serializes and saves the task object created so far in the persistent queue. */
  public void save() {
    taskInfo.save();
  }

  public boolean isTaskRunning() {
    return taskInfo.getTaskState() == TaskInfo.State.Running;
  }

  public boolean hasTaskSucceeded() {
    return taskInfo.getTaskState() == TaskInfo.State.Success;
  }

  public boolean hasTaskFailed() {
    return taskInfo.getTaskState() == TaskInfo.State.Failure;
  }

  /**
   * This method does two things. First, it updates the timestamp on the task to indicate progress.
   * Second, it gives the underlying task to checkpoint its work if needed.
   */
  public void doHeartbeat() {
    // Set the last updated timestamp of the task to now by force saving it.
    taskInfo.markAsDirty();
    taskInfo.save();
  }

  @Override
  public void run() {
    LOG.debug("Running task {}", getTaskUUID());
    task.setUserTaskUUID(getTaskUUID());
    Date executionStart = new Date();
    writeTaskWaitMetric();
    updateTaskState(TaskInfo.State.Running);
    try {
      // Run the task.
      task.run();

      // Update the task state to success and checkpoint it.
      updateTaskState(TaskInfo.State.Success);
      writeTaskSuccessMetric(executionStart);
    } catch (Throwable t) {
      LOG.error("Error running task", t);
      // Update the task state to failure and checkpoint it.
      updateTaskState(TaskInfo.State.Failure);
      writeTaskFailedMetric(executionStart);
    } finally {
      // Update the customer task to a completed state.
      CustomerTask customerTask = CustomerTask.findByTaskUUID(taskInfo.getTaskUUID());
      if (customerTask != null) {
        customerTask.markAsCompleted();
      }

      // In case it was a scheduled task, update state of the task.
      ScheduleTask scheduleTask = ScheduleTask.fetchByTaskUUID(getTaskUUID());
      if (scheduleTask != null) {
        scheduleTask.setCompletedTime();
      }

      // Run a one-off Platform HA sync every time a task finishes.
      replicationManager.oneOffSync();

      // Terminate the task to release resources.
      // Any added subtasks in the subgroups are also terminated.
      task.terminate();
    }
  }

  private void writeTaskWaitMetric() {
    COMMISSIONER_TASK_WAITING_SEC
        .labels(getTaskType())
        .observe(getDurationSeconds(taskInfo.getCreationTime(), new Date()));
  }

  private void writeTaskSuccessMetric(Date executionStart) {
    COMMISSIONER_TASK_EXECUTION_SEC
        .labels(getTaskType(), State.Success.name())
        .observe(getDurationSeconds(executionStart, new Date()));
  }

  private void writeTaskFailedMetric(Date executionStart) {
    COMMISSIONER_TASK_EXECUTION_SEC
        .labels(getTaskType(), State.Failure.name())
        .observe(getDurationSeconds(executionStart, new Date()));
  }

  private String getTaskType() {
    return taskInfo.getTaskType().name();
  }

  /**
   * Updates the task state and saves it to the persistent queue.
   *
   * @param newState
   */
  private void updateTaskState(TaskInfo.State newState) {
    LOG.info("Updating task [" + taskInfo.toString() + "] to new state " + newState);
    taskInfo.setTaskState(newState);
    taskInfo.save();
  }

  @Override
  public String toString() {
    StringBuilder sb = new StringBuilder();
    sb.append("task-info {" + taskInfo.toString() + "}");
    sb.append(", ");
    sb.append("task {" + task.getName() + "}");
    return sb.toString();
  }

  public String toDebugString() {
    StringBuilder sb = new StringBuilder();
    sb.append("task-info {" + taskInfo.toString() + "}");
    sb.append(", ");
    sb.append("task {" + task.toString() + "}");
    return sb.toString();
  }
}
