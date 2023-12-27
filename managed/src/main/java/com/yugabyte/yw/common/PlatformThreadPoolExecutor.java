/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common;

import com.google.common.util.concurrent.ForwardingBlockingQueue;
import com.yugabyte.yw.common.logging.MDCAwareThreadPoolExecutor;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.Executors;
import java.util.concurrent.ThreadFactory;
import java.util.concurrent.TimeUnit;
import lombok.NonNull;
import lombok.extern.slf4j.Slf4j;

/**
 * A subclass of ThreadPoolExecutor that adjusts its pool size depending on the number of tasks
 * before the tasks are queued when the max pool size is reached.
 */
@Slf4j
class PlatformThreadPoolExecutor extends MDCAwareThreadPoolExecutor {

  PlatformThreadPoolExecutor(
      int corePoolSize,
      int maximumPoolSize,
      long keepAliveTime,
      TimeUnit unit,
      BlockingQueue<Runnable> queue) {
    this(
        corePoolSize,
        maximumPoolSize,
        keepAliveTime,
        unit,
        queue,
        Executors.defaultThreadFactory());
  }

  PlatformThreadPoolExecutor(
      int corePoolSize,
      int maximumPoolSize,
      long keepAliveTime,
      TimeUnit unit,
      BlockingQueue<Runnable> queue,
      ThreadFactory threadFactory) {
    super(
        corePoolSize,
        maximumPoolSize,
        keepAliveTime,
        unit,
        new WrappedBlockingQueue<>(queue),
        threadFactory);
    super.setRejectedExecutionHandler(
        (runnable, executor) -> {
          // Add to the original queue
          if (queue.offer(runnable)) {
            if (log.isDebugEnabled()) {
              log.debug(
                  "Queueing task - pool size: {}, active size: {}, "
                      + "max size: {}, queue size: {}",
                  super.getPoolSize(),
                  executor.getActiveCount(),
                  executor.getMaximumPoolSize(),
                  queue.size());
            }
            return;
          }
          log.warn(
              "Queue overflow - task: {}, pool size: {}, active size: {}, "
                  + " max size: {}, queue size: {}",
              runnable.toString(),
              super.getPoolSize(),
              executor.getActiveCount(),
              executor.getMaximumPoolSize(),
              queue.size());
          new AbortPolicy().rejectedExecution(runnable, executor);
        });

    ((WrappedBlockingQueue<Runnable>) super.getQueue()).setExecutor(this);
  }

  private static class WrappedBlockingQueue<E> extends ForwardingBlockingQueue<E> {

    @NonNull private final BlockingQueue<E> queue;

    @NonNull private volatile PlatformThreadPoolExecutor executor;

    WrappedBlockingQueue(BlockingQueue<E> queue) {
      this.queue = queue;
    }

    // The method makes best effort to calculate the queueing condition
    // but wrong decision can happen because it has no control on
    // 1. state transitions of the threads (idle->active etc)
    // 2. queueing order on rejection
    // 3. new thread creation/removal time
    @Override
    public synchronized boolean offer(E e) {
      int currentSize = executor.getPoolSize();
      int activeCount = executor.getActiveCount();
      int availableCount = Math.max(currentSize - activeCount, 0);
      if (currentSize < executor.getMaximumPoolSize()) {
        if (queue.size() >= availableCount) {
          // If there are more tasks (including current task) in the queue
          // than the available threads, acquire more threads.
          // e.g if queue size is zero, the task must not be enqueued if there is a
          // room to acquire a thread because a previous long running task can starve it.
          log.debug(
              "Acquiring new thread - pool size: {}, active size: {}, "
                  + " max size: {}, queue size: {}",
              currentSize,
              activeCount,
              executor.getMaximumPoolSize(),
              queue.size());
          return false;
        }
      }
      if (availableCount == 0 && log.isDebugEnabled()) {
        log.debug(
            "Queueing task - pool size: {}, active size: {}, max size: {}, queue size: {}",
            currentSize,
            activeCount,
            executor.getMaximumPoolSize(),
            queue.size());
      }
      return queue.offer(e);
    }

    public void setExecutor(PlatformThreadPoolExecutor executor) {
      this.executor = executor;
    }

    @Override
    protected BlockingQueue<E> delegate() {
      return queue;
    }
  }
}
