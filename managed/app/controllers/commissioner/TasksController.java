// Copyright (c) YugaByte, Inc.

package controllers.commissioner;

import java.util.Iterator;
import java.util.Map;
import java.util.Map.Entry;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.ThreadFactory;
import java.util.concurrent.atomic.AtomicBoolean;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.util.concurrent.ThreadFactoryBuilder;
import com.google.inject.Inject;

import forms.commissioner.UniverseTaskParams;
import models.commissioner.TaskInfo;
import play.data.Form;
import play.data.FormFactory;
import play.libs.Json;
import play.mvc.Controller;
import play.mvc.Result;

public class TasksController extends Controller {

  public static final Logger LOG = LoggerFactory.getLogger(TasksController.class);

  // Max number of concurrent tasks to execute at a time.
  public static final int NUM_TASK_THREADS = 10;

  // The interval after which progress monitor wakes up and does work.
  public static final long PROGRESS_MONITOR_SLEEP_INTERVAL = 300;

  // State variable which signals if the task manager is shutting down.
  private static AtomicBoolean shuttingDown = new AtomicBoolean(false);

  // The background progress monitor for the tasks.
  static ProgressMonitor progressMonitor;

  // Threadpool to run user submitted tasks.
  static ExecutorService executor;

  // A map of all task UUID's to the task runner objects for all the user tasks that are currently
  // active. Recently completed tasks are also in this list, their completion percentage should be
  // persisted before removing the task from this map.
  static Map<UUID, TaskRunner> runningTasks = new ConcurrentHashMap<UUID, TaskRunner>();

  @Inject
  FormFactory formFactory;

  static {
    // Initialize the tasks threadpool.
    ThreadFactory namedThreadFactory =
        new ThreadFactoryBuilder().setNameFormat("TaskPool-%d").build();
    executor = Executors.newFixedThreadPool(NUM_TASK_THREADS, namedThreadFactory);
    LOG.info("Started TaskPool with " + NUM_TASK_THREADS + " threads.");

    // Initialize the task manager.
    progressMonitor = new ProgressMonitor();
    progressMonitor.start();
    LOG.info("Started TaskProgressMonitor thread.");
  }

  /**
   * Creates a new task runner to run the required task, and submits it to a threadpool if needed.
   * Currently used for create universe or edit universe tasks.
   *
   * @return Success if the task was successfully queued. Error otherwise.
   */
  public Result createOrEdit(UUID universeUUID) {
    // TODO: Decide if we need to check auth token in the Commissioner. If so make that check common
    // across all controllers.

    ObjectNode responseJson = Json.newObject();

    // Get the params for the task.
    Form<UniverseTaskParams> formData =
        formFactory.form(UniverseTaskParams.class).bindFromRequest();
    if (formData.hasErrors()) {
      responseJson.set("error", formData.errorsAsJson());
      return badRequest(responseJson);
    }
    UniverseTaskParams taskParams = formData.get();

    TaskInfo.Type taskType = (taskParams.create) ? TaskInfo.Type.CreateUniverse
                                                 : TaskInfo.Type.EditUniverse;

    // Initialize the non-form related fields.
    taskParams.universeUUID = universeUUID;

    try {
      // Claim the task if we can - check if we will go above the max local concurrent task
      // threshold. If we can claim it, set ourselves as the owner of the task. Otherwise, do not
      // claim the task so that some other process can claim it.
      // TODO: enforce a limit on number of tasks here.
      boolean claimTask = true;

      // Create the task runner object based on the various parameters passed in.
      TaskRunner taskRunner =
          TaskRunner.createTask(taskType, taskParams, claimTask);

      if (claimTask) {
        // Add this task to our queue.
        runningTasks.put(taskRunner.getTaskUUID(), taskRunner);

        // If we had claimed ownership of the task, submit it to the task threadpool.
        executor.submit(taskRunner);
      }

      responseJson.put("taskUUID", taskRunner.getTaskUUID().toString());
      return ok(responseJson);
    } catch (Throwable t) {
      LOG.error("Error processing task type " + taskType, t);
    }
    responseJson.put("error", "Error processing task type " + taskType);
    return internalServerError(responseJson);
  }

  public Result show(UUID taskUUID) {
    ObjectNode responseJson = Json.newObject();

    // Find the task runner for the task.
    TaskRunner taskRunner = runningTasks.get(taskUUID);
    if (taskRunner != null) {
      // Find out the state of the task.
      responseJson.put("status", taskRunner.getState());
      // Find out the percentage completion.
      responseJson.put("percent", taskRunner.getPercentCompleted());
      return ok(responseJson);
    }

    // Check if the task is in the DB as it completed and is no longer in 'runningTasks'.
    TaskInfo taskInfo = TaskInfo.get(taskUUID);
    if (taskInfo != null) {

      // Find out the state of the task.
      responseJson.put("status", taskInfo.getTaskState().toString());
      // The job is assumed to be completed if its in the Tasks table and not in memory.
      responseJson.put("percent", (taskInfo.getTaskState() == TaskInfo.State.Failure ? 0 : 100));
      return ok(responseJson);
    }

    // We are not able to find the task. Report an error.
    LOG.error("Not able to find task " + taskUUID);
    responseJson.put("error", "Not able to find task " + taskUUID);
    return internalServerError(responseJson);


  }

  /**
   * A progress monitor to constantly write a last updated timestamp in the DB so that this
   * process and all its subtasks are considered to be alive.
   */
  private static class ProgressMonitor extends Thread {

    public ProgressMonitor() {
      setName("TaskProgressMonitor");
    }

    @Override
    public void run() {
      while (!shuttingDown.get()) {
        // Loop through all the active tasks.
        Iterator<Entry<UUID, TaskRunner>> iter = runningTasks.entrySet().iterator();
        while (iter.hasNext()) {
          Entry<UUID, TaskRunner> entry = iter.next();
          TaskRunner taskRunner = entry.getValue();

          // If the task is still running, update its latest timestamp as a part of the heartbeat.
          if (taskRunner.isTaskRunning()) {
            taskRunner.doHeartbeat();
          }
          else if (taskRunner.hasTaskSucceeded()) {
            LOG.info("Task " + taskRunner.toString() + " has succeeded.");
            // Remove task from the set of live tasks.
            iter.remove();
          }
          else if (taskRunner.hasTaskFailed()) {
            LOG.info("Task " + taskRunner.toString() + " has failed.");
            // Remove task from the set of live tasks.
            iter.remove();
          }
        }

        // TODO: Scan the DB for tasks that have failed to make progress and claim one if possible.

        // Sleep for the required interval.
        try {
          Thread.sleep(TasksController.PROGRESS_MONITOR_SLEEP_INTERVAL);
        } catch (InterruptedException e) { }
      }
    }
  }
}
