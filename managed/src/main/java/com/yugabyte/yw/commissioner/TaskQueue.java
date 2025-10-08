// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner;

import static com.google.common.base.Preconditions.checkState;
import static play.mvc.Http.Status.CONFLICT;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.cronutils.utils.VisibleForTesting;
import com.google.inject.Singleton;
import com.yugabyte.yw.commissioner.TaskExecutor.RunnableTask;
import com.yugabyte.yw.commissioner.TaskExecutor.TaskParams;
import com.yugabyte.yw.commissioner.TaskQueue.Queue.OpType;
import com.yugabyte.yw.common.PlatformExecutorFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ShutdownHookHandler;
import com.yugabyte.yw.common.TaskExecutionException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.concurrent.KeyLock;
import com.yugabyte.yw.common.logging.LogUtil;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.models.TaskInfo.State;
import com.yugabyte.yw.models.helpers.YBAError.Code;
import jakarta.inject.Inject;
import java.time.Duration;
import java.time.Instant;
import java.util.Iterator;
import java.util.Map;
import java.util.Objects;
import java.util.UUID;
import java.util.concurrent.CancellationException;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicReference;
import java.util.function.BiConsumer;
import java.util.function.Function;
import java.util.function.Predicate;
import lombok.extern.slf4j.Slf4j;
import org.slf4j.MDC;

/** Queue for tasks for each resource target. */
@Slf4j
@Singleton
public class TaskQueue {
  private static final int DEFAULT_QUEUE_CAPACITY = 2;

  // Capacity for each target queue.
  private final int capacity;
  // Target UUID (e.g universe UUID) to Queue of tasks.
  private final Map<UUID, Queue> targetTaskQueues = new ConcurrentHashMap<>();
  // Task UUID to target UUIDs.
  private final Map<UUID, UUID> taskTargets = new ConcurrentHashMap<>();
  // Key lock for targets.
  private final KeyLock<UUID> targetKeyLock = new KeyLock<>();

  @Inject
  public TaskQueue(
      ShutdownHookHandler shutdownHookHandler, PlatformExecutorFactory platformExecutorFactory) {
    this(DEFAULT_QUEUE_CAPACITY);
    shutdownHookHandler.addShutdownHook(this, tq -> tq.shutdown(), 120 /* weight */);
  }

  @VisibleForTesting
  TaskQueue(int capacity) {
    this.capacity = capacity;
  }

  // Minimal linked list to allow removal by node directly.
  static class Queue {
    private final BiConsumer<Node, OpType> listener;
    private final int capacity;

    private Node head;
    private Node tail;
    private int size;

    enum OpType {
      ADD,
      REMOVE
    }

    private Queue(int capacity, BiConsumer<Node, OpType> listener) {
      this.capacity = capacity;
      this.listener = listener;
    }

    synchronized int size() {
      return size;
    }

    synchronized void ensureCapacity() {
      if (capacity <= size) {
        throw new PlatformServiceException(
            CONFLICT, "Queue is already full with max capacity " + capacity);
      }
    }

    synchronized boolean add(Node node) {
      checkState(node != null, "Node cannot be null");
      ensureCapacity();
      log.debug(
          "Queuing task {}. Existing queue size is {}", node.taskRunnable.getTaskType(), size());
      node.next = null;
      node.previous = null;
      if (head == null || tail == null) {
        head = node;
        tail = node;
      } else {
        tail.next = node;
        node.previous = tail;
        tail = node;
      }
      node.isMember = true;
      size++;
      listener.accept(node, OpType.ADD);
      return true;
    }

    synchronized Node peek() {
      return head;
    }

    synchronized boolean remove(Node node) {
      checkState(size > 0, "Size must be non-zero");
      if (node == null || !node.isMember || size <= 0) {
        return false;
      }
      log.debug(
          "Removing task {}. Existing queue size is {}", node.taskRunnable.getTaskType(), size());
      if (node == head) {
        head = node.next;
      }
      if (node == tail) {
        tail = node.previous;
      }
      if (node.previous != null) {
        node.previous.next = node.next;
      }
      if (node.next != null) {
        node.next.previous = node.previous;
      }
      node.isMember = false;
      node.previous = null;
      node.next = null;
      size--;
      listener.accept(node, OpType.REMOVE);
      return true;
    }

    synchronized void remove(Predicate<Node> predicate) {
      Node node = head;
      while (node != null) {
        checkState(size > 0, "Size must be non-zero");
        Node next = node.next;
        if (predicate.test(node)) {
          remove(node);
        }
        node = next;
      }
    }

    synchronized Node find(Predicate<Node> predicate) {
      Node node = head;
      while (node != null) {
        Node next = node.next;
        if (predicate.test(node)) {
          return node;
        }
        node = next;
      }
      return null;
    }
  }

  @VisibleForTesting
  /* Node for the doubly linked list. */
  static class Node {
    private volatile boolean isMember;
    private Node previous;
    private Node next;

    final RunnableTask taskRunnable;
    final ITaskParams taskParams;
    final String correlationId;

    Node(RunnableTask taskRunnable, ITaskParams taskParams, String correlationId) {
      this.taskRunnable = taskRunnable;
      this.taskParams = taskParams;
      this.correlationId = correlationId;
    }
  }

  private void shutdown() {
    Iterator<Map.Entry<UUID, Queue>> iter = targetTaskQueues.entrySet().iterator();
    while (iter.hasNext()) {
      Map.Entry<UUID, Queue> entry = iter.next();
      targetKeyLock.acquireLock(entry.getKey());
      try {
        Queue queue = entry.getValue();
        queue.remove(
            n -> {
              if (n.taskRunnable.isRunning()) {
                // Task executor will handle it.
                return false;
              }
              n.taskRunnable.updateTaskDetailsOnError(
                  State.Aborted, new CancellationException("Platform shutdown"));
              return true;
            });
      } finally {
        targetKeyLock.releaseLock(entry.getKey());
      }
      iter.remove();
    }
  }

  private Queue getOrCreateQueue(UUID targetUuid) {
    return targetTaskQueues.computeIfAbsent(
        targetUuid,
        k ->
            new Queue(
                this.capacity,
                (node, opType) -> {
                  if (opType == OpType.ADD) {
                    taskTargets.put(node.taskRunnable.getTaskUUID(), targetUuid);
                  } else if (opType == OpType.REMOVE) {
                    taskTargets.remove(node.taskRunnable.getTaskUUID());
                  }
                }));
  }

  private Node createQueueNode(
      TaskParams taskParams,
      Function<TaskParams, RunnableTask> taskRunnnableCreator,
      Duration queueWaitTime) {
    String correlationId = MDC.get(LogUtil.CORRELATION_ID);
    if (correlationId == null) {
      correlationId = UUID.randomUUID().toString();
    }
    RunnableTask taskRunnable = taskRunnnableCreator.apply(taskParams);
    return new Node(taskRunnable, taskParams.getTaskParams(), correlationId);
  }

  private Duration getQueueWaitTime(TaskParams taskParams, RunnableTask taskRunnable) {
    Duration waitTime =
        taskRunnable
            .getTask()
            .getQueueWaitTime(taskParams.getTaskType(), taskParams.getTaskParams());
    if (waitTime == null) {
      log.error(
          "Task {} is not queuable on existing task {}({})",
          taskParams.getTaskType(),
          taskRunnable.getTaskType(),
          taskRunnable.getTaskUUID());
      throw new PlatformServiceException(
          CONFLICT,
          String.format(
              "Task %s cannot be queued on existing task %s",
              taskParams.getTaskType(), taskRunnable.getTaskType()));
    }
    return waitTime;
  }

  public int size(UUID targetUuid) {
    Queue queue = targetTaskQueues.get(targetUuid);
    return queue == null ? 0 : queue.size();
  }

  /**
   * Queue the given task with the task params.
   *
   * @param taskParams the task params.
   * @param taskRunnnableCreator the runnable task creator callback.
   * @param taskRunnableConsumer the task submitter callback.
   * @return the queued runnable task.
   */
  public RunnableTask enqueue(
      TaskParams taskParams,
      Function<TaskParams, RunnableTask> taskRunnnableCreator,
      BiConsumer<RunnableTask, ITaskParams> taskRunnableConsumer) {
    UUID targetUuid = taskParams.getTaskParams().getTargetUuid(taskParams.getTaskType());
    if (targetUuid == null) {
      log.info("Unknown target for task {}. Queuing is not supported", taskParams.getTaskType());
      RunnableTask taskRunnable =
          Objects.requireNonNull(taskRunnnableCreator.apply(taskParams), "Runnable task is null");
      taskRunnableConsumer.accept(taskRunnable, taskParams.getTaskParams());
      return taskRunnable;
    }
    targetKeyLock.acquireLock(targetUuid);
    try {
      Queue queue = getOrCreateQueue(targetUuid);
      Node head = queue.size == 0 ? null : queue.peek();
      Duration queueWaitTime =
          head == null ? Duration.ZERO : getQueueWaitTime(taskParams, head.taskRunnable);
      queue.ensureCapacity();
      Node node =
          createQueueNode(
              taskParams, taskRunnnableCreator, queueWaitTime.plus(Duration.ofMinutes(1)));
      // Always add to the queue to keep track of in-progress task too.
      queue.add(node);
      if (head == null) {
        try {
          taskRunnableConsumer.accept(node.taskRunnable, taskParams.getTaskParams());
        } catch (RuntimeException e) {
          queue.remove(node);
          throw e;
        }
      } else if (head.taskRunnable.isRunning()) {
        log.info(
            "Aborting the currently running task {} in {} secs",
            head.taskRunnable.getTaskType(),
            queueWaitTime.getSeconds());
        final Instant now = Instant.now();
        // This is to cache the queue wait time to avoid duplicate calculation.
        final AtomicReference<Pair<Duration, Instant>> waitAbortTime =
            new AtomicReference<>(new Pair<>(queueWaitTime, now.plus(queueWaitTime)));
        head.taskRunnable.setAbortTimeSupplier(
            () -> {
              Duration waitTime = getQueueWaitTime(taskParams, head.taskRunnable);
              return waitAbortTime
                  .updateAndGet(
                      p ->
                          p.getFirst().equals(waitTime)
                              ? p
                              : new Pair<>(waitTime, now.plus(waitTime)))
                  .getSecond();
            });
      } else {
        // This is not expected to happen.
        // Fail and remove all the tasks in the queue if it happens due to bugs.
        String errMsg =
            String.format(
                "Task %s cannot be queued as the first task %s in the queue is not running",
                node.taskRunnable.getTaskType(), head.taskRunnable.getTaskType());
        queue.remove(
            n -> {
              n.taskRunnable.updateTaskDetailsOnError(
                  State.Failure, new TaskExecutionException(Code.INTERNAL_ERROR, errMsg));
              return true;
            });
        log.error(
            "Task {}({}) cannot be queued as the first task {}({}) is not running. Queue size: {}",
            node.taskRunnable.getTaskType(),
            node.taskRunnable.getTaskUUID(),
            head.taskRunnable.getTaskType(),
            head.taskRunnable.getTaskUUID(),
            queue.size());
        throw new PlatformServiceException(INTERNAL_SERVER_ERROR, errMsg);
      }
      return node.taskRunnable;
    } finally {
      targetKeyLock.releaseLock(targetUuid);
    }
  }

  /**
   * Remove the completed task from the queue and submit the next waiting task.
   *
   * @param completedTask the completed task.
   * @param taskRunnableConsumer the task submitter callback.
   * @return the submitted task.
   */
  public RunnableTask dequeue(
      RunnableTask completedTask, BiConsumer<RunnableTask, ITaskParams> taskRunnableConsumer) {
    UUID targetUuid = taskTargets.get(completedTask.getTaskUUID());
    if (targetUuid == null) {
      log.info(
          "Task {}({}) was not queued", completedTask.getTaskType(), completedTask.getTaskUUID());
    } else {
      targetKeyLock.acquireLock(targetUuid);
      try {
        Queue queue = targetTaskQueues.get(targetUuid);
        if (queue != null) {
          while (queue.size() > 0) {
            Node head = queue.peek();
            if (head.taskRunnable.hasTaskCompleted()) {
              log.debug("Removing completed task {}", head.taskRunnable.getTaskType());
              queue.remove(head);
            } else {
              log.debug("Submitting next task {}", head.taskRunnable.getTaskType());
              try {
                return Util.doWithCorrelationId(
                    head.correlationId,
                    id -> {
                      taskRunnableConsumer.accept(head.taskRunnable, head.taskParams);
                      return head.taskRunnable;
                    });
              } catch (Exception e) {
                log.debug("Removing failed task {}", head.taskRunnable.getTaskType());
                queue.remove(head);
                head.taskRunnable.updateTaskDetailsOnError(State.Failure, e);
                log.error("Error in submitting task {}", head.taskRunnable.getTaskType());
              }
            }
          }
          if (queue.size() == 0) {
            targetTaskQueues.remove(targetUuid);
          }
        }
      } catch (Exception e) {
        log.error("Error in dequeing task for target {}", targetUuid, e);
      } finally {
        targetKeyLock.releaseLock(targetUuid);
      }
    }
    return null;
  }

  /**
   * Find the task with the given UUID in the queue.
   *
   * @param taskUuid the task UUID.
   * @return the runnable task if it is found else null.
   */
  public RunnableTask find(UUID taskUuid) {
    UUID targetUuid = taskTargets.get(taskUuid);
    if (targetUuid != null) {
      targetKeyLock.acquireLock(targetUuid);
      try {
        Queue queue = targetTaskQueues.get(targetUuid);
        Node node =
            queue == null ? null : queue.find(n -> taskUuid.equals(n.taskRunnable.getTaskUUID()));
        if (node != null) {
          return node.taskRunnable;
        }
      } finally {
        targetKeyLock.releaseLock(targetUuid);
      }
    }
    return null;
  }

  /**
   * Cancel or remove a queued task that is still not running.
   *
   * @param taskUuid the task UUID to be cancelled.
   * @return true if it is found and removed.
   */
  public boolean cancel(UUID taskUuid) {
    UUID targetUuid = taskTargets.get(taskUuid);
    if (targetUuid != null) {
      targetKeyLock.acquireLock(targetUuid);
      try {
        Queue queue = targetTaskQueues.get(targetUuid);
        Node node =
            queue == null ? null : queue.find(n -> taskUuid.equals(n.taskRunnable.getTaskUUID()));
        if (node != null && !node.taskRunnable.isRunning()) {
          queue.remove(node);
          String msg =
              String.format(
                  "Task %s(%s) is aborted while waiting in the queue",
                  node.taskRunnable.getTaskType(), node.taskRunnable.getTaskUUID());
          log.info(msg);
          node.taskRunnable.updateTaskDetailsOnError(State.Aborted, new CancellationException(msg));
          return true;
        }
      } finally {
        targetKeyLock.releaseLock(targetUuid);
      }
    }
    return false;
  }
}
