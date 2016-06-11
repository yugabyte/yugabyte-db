// Copyright (c) Yugabyte, Inc.

package controllers.commissioner;

import java.util.Iterator;
import java.util.Queue;
import java.util.concurrent.ConcurrentLinkedQueue;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.ThreadFactory;
import java.util.concurrent.atomic.AtomicBoolean;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.google.common.util.concurrent.ThreadFactoryBuilder;
import com.google.inject.Inject;

import forms.commissioner.CreateInstanceTaskParams;
import models.commissioner.TaskInfo;
import play.data.Form;
import play.data.FormFactory;
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

  // The queue of all user tasks that are currently active.
  static Queue<TaskRunner> runningTasks = new ConcurrentLinkedQueue<TaskRunner>();

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
   *
   * @return Success if the task was successfully queued. Error otherwise.
   */
  public Result create() {
    // TODO: Decide if we need to check auth token in the Commissioner. If so make that check common
    // across all controllers.

    TaskInfo.Type taskType = TaskInfo.Type.CreateInstance;

    // Get the params for the task.
    Form<CreateInstanceTaskParams> formData =
        formFactory.form(CreateInstanceTaskParams.class).bindFromRequest();
    if (formData.hasErrors()) {
      return badRequest(formData.errorsAsJson());
    }
    CreateInstanceTaskParams taskParams = formData.get();

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
        runningTasks.add(taskRunner);

        // If we had claimed ownership of the task, submit it to the task threadpool.
        executor.submit(taskRunner);
      }

      return ok(taskRunner.getTaskUUID().toString());
    } catch (Throwable t) {
      LOG.error("Error processing task type " + taskType, t);
    }
    return internalServerError("Error processing task type " + taskType);
  }

  private static class ProgressMonitor extends Thread {

    public ProgressMonitor() {
      setName("TaskProgressMonitor");
    }

    @Override
    public void run() {
      while (!shuttingDown.get()) {
        // Loop through all the active tasks.
        Iterator<TaskRunner> iter = runningTasks.iterator();
        while (iter.hasNext()) {
          TaskRunner taskRunner = iter.next();

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
