package com.yugabyte.yw.common.operator.utils;

import com.google.common.annotations.VisibleForTesting;
import com.google.common.util.concurrent.ThreadFactoryBuilder;
import com.yugabyte.yw.common.utils.Pair;
import java.util.Random;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;
import lombok.extern.slf4j.Slf4j;

// TODO: Implement BlockingQueue interface for easy of use.
// OperatorWorkQueue is currently a basic abstraction around a BlockingArrayQueue, but the
// abstraction will allow us to later implement a more complex data structure that takes currently
// queued resources into account.
@Slf4j
public class OperatorWorkQueue {
  public static final int WORKQUEUE_CAPACITY = 1024;
  // For scheduled thread pool, this acts like max size of pool
  private static final int SCHEDULED_THREAD_POOL_SIZE = 5;
  private static final int INITIAL_BACKOFF_TIME_SEC = 15;
  private static final int RANDOM_RANGE_MIN = 20;
  private static final int RANDOM_RANGE_MAX = 30;
  private static final int MAX_BACKOFF_TIME_SEC = 1800;

  public static enum ResourceAction {
    CREATE,
    UPDATE,
    DELETE,
    NO_OP;

    public boolean needsNoOpAction() {
      return this.equals(CREATE) || this.equals(UPDATE) || this.equals(DELETE);
    }
  }

  private final ArrayBlockingQueue<Pair<String, ResourceAction>> workQueue;
  private final ScheduledExecutorService scheduledExecutorService;
  // Stores retry count for each resource
  private final ConcurrentHashMap<String, Integer> retryCountMap;
  // Stores boolean flag for whether resource is locked for requeues of Update or Create
  // Key added when resource has Create Action and removed when resource receives Delete.
  private final ConcurrentHashMap<String, Boolean> universeLockedForRequeueMap;
  private final ConcurrentHashMap<String, Boolean> noOpLockMap;
  private final int initialBackoffTimeSec;
  private final int randomRangeMin;
  private final int randomRangeMax;

  public OperatorWorkQueue() {
    this(INITIAL_BACKOFF_TIME_SEC, RANDOM_RANGE_MIN, RANDOM_RANGE_MAX);
  }

  public OperatorWorkQueue(int initialBackoffTime, int randomRangeMin, int randomRangeMax) {
    this.workQueue = new ArrayBlockingQueue<>(WORKQUEUE_CAPACITY);
    this.scheduledExecutorService =
        Executors.newScheduledThreadPool(
            SCHEDULED_THREAD_POOL_SIZE,
            new ThreadFactoryBuilder().setNameFormat("TaskPool-KubernetesOperator-%d").build());
    this.retryCountMap = new ConcurrentHashMap<>();
    this.universeLockedForRequeueMap = new ConcurrentHashMap<>();
    this.noOpLockMap = new ConcurrentHashMap<>();
    this.initialBackoffTimeSec = initialBackoffTime;
    this.randomRangeMin = randomRangeMin;
    this.randomRangeMax = randomRangeMax;
  }

  private boolean getUniverseLockedForActionRequeue(String resourceName, ResourceAction action) {
    if (action.equals(ResourceAction.CREATE) || action.equals(ResourceAction.UPDATE)) {
      return universeLockedForRequeueMap.getOrDefault(resourceName, false);
    } else if (action.equals(ResourceAction.NO_OP)) {
      return noOpLockMap.getOrDefault(resourceName, false);
    }
    return false;
  }

  private void updateUniverseLockedRequeueAction(String resourceName, ResourceAction action) {
    if ((action.equals(ResourceAction.CREATE) || action.equals(ResourceAction.UPDATE))
        && universeLockedForRequeueMap.containsKey(resourceName)) {
      log.debug("Locked Universe {} for Create/Update requeues", resourceName);
      universeLockedForRequeueMap.put(resourceName, true);
    } else if (action.equals(ResourceAction.NO_OP) && noOpLockMap.containsKey(resourceName)) {
      log.debug("Locked Universe {} for No-Op requeues", resourceName);
      noOpLockMap.put(resourceName, true);
    }
  }

  private synchronized boolean maybeGetAndLockUniverseForRequeue(
      String resourceName, ResourceAction action) {
    boolean universeLocked = getUniverseLockedForActionRequeue(resourceName, action);
    if (!universeLocked) {
      updateUniverseLockedRequeueAction(resourceName, action);
    }
    return universeLocked;
  }

  private void onRemove(String resourceName, ResourceAction action) {
    if ((action.equals(ResourceAction.CREATE) || action.equals(ResourceAction.UPDATE))
        && universeLockedForRequeueMap.containsKey(resourceName)) {
      log.debug("Unlocked Universe {} for Create/Update requeues", resourceName);
      universeLockedForRequeueMap.put(resourceName, false);
    } else if (action.equals(ResourceAction.NO_OP) && noOpLockMap.containsKey(resourceName)) {
      log.debug("Unlocked Universe {} for No-Op requeues", resourceName);
      noOpLockMap.put(resourceName, false);
    }
  }

  private void onAdd(String resourceName, ResourceAction action) {
    if (action.equals(ResourceAction.CREATE)
        && !universeLockedForRequeueMap.containsKey(resourceName)) {
      log.debug("Locked Universe {} for Create/Update requeues with new entry", resourceName);
      universeLockedForRequeueMap.put(resourceName, true);
      log.debug("Added Universe {} to No-op lock map", resourceName);
      noOpLockMap.put(resourceName, false);
    } else if (action.equals(ResourceAction.DELETE)) {
      log.debug("Removed Universe {} from requeue lock map", resourceName);
      universeLockedForRequeueMap.remove(resourceName);
      log.debug("Removed Universe {} from No-Op lock map", resourceName);
      noOpLockMap.remove(resourceName);
    } else if (action.equals(ResourceAction.NO_OP) && noOpLockMap.containsKey(resourceName)) {
      log.debug("Locked Universe {} for No-Op requeues", resourceName);
      noOpLockMap.put(resourceName, true);
    }
  }

  public synchronized boolean add(Pair<String, ResourceAction> item) {
    try {
      log.debug("Adding {} action for Universe {}", item.getSecond().name(), item.getFirst());
      onAdd(item.getFirst(), item.getSecond());
      workQueue.put(item);
      return true;
    } catch (InterruptedException e) {
      log.trace("Interrupted", e);
      return false;
    }
  }

  public Pair<String, ResourceAction> peek() {
    return workQueue.peek();
  }

  // Get the next work item with a timeout on the wait.

  @VisibleForTesting
  protected Pair<String, ResourceAction> pop(long timeoutSeconds) {
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
      Pair<String, ResourceAction> pair = workQueue.take();
      onRemove(pair.getFirst(), pair.getSecond());
      return pair;
    } catch (InterruptedException e) {
      log.warn("Interrupted", e);
      return null;
    }
  }

  public boolean isEmpty() {
    return workQueue.isEmpty();
  }

  public synchronized void requeue(
      String resourceName, ResourceAction action, boolean incrementRetry) {
    if (maybeGetAndLockUniverseForRequeue(resourceName, action)) {
      log.debug("Universe {} already has {} action requeued", resourceName, action);
      return;
    }
    int retryCount = retryCountMap.getOrDefault(resourceName, 0);
    Random random = new Random();
    long backoffTime =
        action.equals(ResourceAction.NO_OP)
            ? initialBackoffTimeSec
            : retryCount * initialBackoffTimeSec + random.nextInt(randomRangeMin, randomRangeMax);
    if (backoffTime > MAX_BACKOFF_TIME_SEC) {
      backoffTime = MAX_BACKOFF_TIME_SEC + random.nextInt(100, 150);
    }
    scheduledExecutorService.schedule(
        () -> {
          log.trace("Requeuing {} task for {}", action, resourceName);
          add(new Pair<String, ResourceAction>(resourceName, action));
          if (action.needsNoOpAction()) {
            try {
              // Requeuing NO_OP with slight delay.
              Thread.sleep(5000);
            } catch (InterruptedException e) {
              log.trace("Requeuing NO_OP action delay interrupted");
            }
            log.trace("Requeuing {} task for {}", ResourceAction.NO_OP, resourceName);
            add(new Pair<String, ResourceAction>(resourceName, ResourceAction.NO_OP));
          }
        },
        backoffTime,
        TimeUnit.SECONDS);
    if (incrementRetry) {
      log.debug("Retry count for Resource {}: {}", resourceName, retryCount + 1);
      retryCountMap.put(resourceName, retryCount + 1);
    }
  }

  public void resetRetries(String resourceName) {
    if (retryCountMap.containsKey(resourceName)) {
      log.debug("Clearing retries for resource {}", resourceName);
      retryCountMap.remove(resourceName);
    }
  }

  public void clearState(String resourceName) {
    retryCountMap.remove(resourceName);
    universeLockedForRequeueMap.remove(resourceName);
    noOpLockMap.remove(resourceName);
  }
}
