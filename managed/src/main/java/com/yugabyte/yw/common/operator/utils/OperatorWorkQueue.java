package com.yugabyte.yw.common.operator.utils;

import com.yugabyte.yw.common.utils.Pair;
import java.util.Timer;
import java.util.TimerTask;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.TimeUnit;
import lombok.extern.slf4j.Slf4j;

// TODO: Implement BlockingQueue interface for easy of use.
// OperatorWorkQueue is currently a basic abstraction around a BlockingArrayQueue, but the
// abstraction will allow us to later implement a more complex data structure that takes currently
// queued resources into account.
@Slf4j
public class OperatorWorkQueue {
  public static final int WORKQUEUE_CAPACITY = 1024;

  public static enum ResourceAction {
    CREATE,
    UPDATE,
    DELETE
  }

  private final ArrayBlockingQueue<Pair<String, ResourceAction>> workQueue;

  public OperatorWorkQueue() {
    this.workQueue = new ArrayBlockingQueue<>(WORKQUEUE_CAPACITY);
  }

  public boolean add(Pair<String, ResourceAction> item) {
    return workQueue.add(item);
  }

  public Pair<String, ResourceAction> peek() {
    return workQueue.peek();
  }

  // Get the next work item with a timeout on the wait.
  public Pair<String, ResourceAction> pop(long timeoutSeconds) {
    if (timeoutSeconds < 0) {
      throw new IllegalArgumentException("timeoutSeconds must be greater then or equal to 0");
    }
    try {
      return workQueue.poll(timeoutSeconds, TimeUnit.SECONDS);
    } catch (InterruptedException e) {
      log.warn("Interrupted", e);
      return null;
    }
  }

  // Get the next work item, blocking until it returns.
  public Pair<String, ResourceAction> pop() {
    try {
      return workQueue.take();
    } catch (InterruptedException e) {
      log.warn("Interrupted", e);
      return null;
    }
  }

  public boolean isEmpty() {
    return workQueue.isEmpty();
  }

  public void requeue(String resourceName, ResourceAction action) {
    log.warn("universe {} is locked, requeue update and try again later", resourceName);
    Timer timer = new Timer();
    TimerTask task =
        new TimerTask() {
          @Override
          public void run() {
            log.trace("requeueing task for {}", resourceName);
            add(new Pair<String, OperatorWorkQueue.ResourceAction>(resourceName, action));
          }
        };
    timer.schedule(task, 30000);
  }
}
