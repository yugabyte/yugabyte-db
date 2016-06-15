package controllers.commissioner;

import java.util.LinkedList;
import java.util.List;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Future;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class TaskList implements Runnable {

  public static final Logger LOG = LoggerFactory.getLogger(TaskList.class);

  // Task list name.
  private String name;

  // The list of tasks in this task list.
  private List<ITask> taskList;

  // The list of futures to wait for.
  private List<Future<?>> futuresList;

  private boolean allTasksSucceeded = true;

  // The number of threads to run in parallel.
  int numThreads;

  // The threadpool executor in case parallel execution is requested.
  ExecutorService executor;

  // Flag to denote the task is done.
  boolean tasksDone = false;

  /**
   * Creates the task list.
   *
   * @param name     : Name for the task list, used to name the threads.
   * @param executor : The threadpool to run the task on.
   */
  public TaskList(String name, ExecutorService executor) {
    this.name = name;
    this.executor = executor;
    this.taskList = new LinkedList<ITask>();
    this.futuresList = new LinkedList<Future<?>>();
  }

  public void addTask(ITask task) {
    LOG.info("Adding task #" + taskList.size() + ": " + task.toString());
    taskList.add(task);
  }

  /**
   * Asynchronously starts the tasks and returns. To wait for the tasks to complete, call the
   * waitFor() method.
   */
  @Override
  public void run() {
    if (taskList.isEmpty()) {
      LOG.error("No tasks in task list " + name);
      tasksDone = true;
      return;
    }
    LOG.info("Running task list " + name);
    for (ITask task : taskList) {
      Future<?> future = executor.submit(task);
      futuresList.add(future);
    }
  }

  public void waitFor() {
    for (Future<?> future : futuresList) {
      // Wait for each future to finish.
      try {
        if (future.get() == null) {
          // Task succeeded.
        } else {
          allTasksSucceeded = false;
        }
      } catch (InterruptedException e) {
        allTasksSucceeded = false;
        LOG.error("Failed to execute task", e);
      } catch (ExecutionException e) {
        allTasksSucceeded = false;
        LOG.error("Failed to execute task", e);
      }
    }
  }
}
