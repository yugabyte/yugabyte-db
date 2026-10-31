/*
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.common;

import com.google.common.util.concurrent.MoreExecutors;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.typesafe.config.Config;
import java.time.Duration;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.SynchronousQueue;
import java.util.concurrent.ThreadFactory;
import java.util.concurrent.ThreadPoolExecutor;
import java.util.concurrent.TimeUnit;
import lombok.extern.slf4j.Slf4j;

/** For easy and configurable creation of executor that will shutdown on app shutdown. */
@Slf4j
@Singleton
public class PlatformExecutorFactory {

  public static final int SHUTDOWN_TIMEOUT_MINUTES = 1;

  private final Config config;
  private final ShutdownHookHandler shutdownHookHandler;

  @Inject
  public PlatformExecutorFactory(Config config, ShutdownHookHandler shutdownHookHandler) {
    this.config = config;
    this.shutdownHookHandler = shutdownHookHandler;
  }

  private int ybCorePoolSize(String poolName) {
    return config.getInt(getPath(poolName, ".core_threads"));
  }

  private int ybMaxPoolSize(String poolName) {
    return config.getInt(getPath(poolName, ".max_threads"));
  }

  private Duration keepAliveDuration(String poolName) {
    return config.getDuration(getPath(poolName, ".thread_ttl"));
  }

  private int ybQueueCapacity(String poolName) {
    return config.getInt(getPath(poolName, ".queue_capacity"));
  }

  private Duration coalescingDelay(String poolName) {
    return config.getDuration(getPath(poolName, ".coalescing_delay"));
  }

  private String getPath(String poolName, String confKey) {
    return "yb." + poolName + confKey;
  }

  public ThreadPoolExecutor createExecutor(
      String configPoolName, ThreadFactory namedThreadFactory) {
    return createExecutor(
        configPoolName,
        ybCorePoolSize(configPoolName),
        ybMaxPoolSize(configPoolName),
        Duration.ofSeconds(keepAliveDuration(configPoolName).getSeconds()),
        ybQueueCapacity(configPoolName),
        namedThreadFactory);
  }

  /**
   * Like {@link #createExecutor(String, ThreadFactory)}, but a task submitted while the pool and
   * its queue are full is rejected right away instead of blocking the submitting thread. The pool
   * is not shut down with the application shutdown hooks: the caller shuts it down.
   */
  public ThreadPoolExecutor createNonBlockingExecutor(
      String configPoolName, ThreadFactory namedThreadFactory) {
    return createExecutor(
        configPoolName,
        ybCorePoolSize(configPoolName),
        ybMaxPoolSize(configPoolName),
        Duration.ofSeconds(keepAliveDuration(configPoolName).getSeconds()),
        newQueue(ybQueueCapacity(configPoolName)),
        namedThreadFactory,
        Duration.ZERO,
        false);
  }

  public ThreadPoolExecutor createExecutor(
      String poolName, int corePoolSize, int maxPoolSize, ThreadFactory namedThreadFactory) {
    return createExecutor(
        poolName, corePoolSize, maxPoolSize, Duration.ZERO, Integer.MAX_VALUE, namedThreadFactory);
  }

  public ThreadPoolExecutor createFixedExecutor(
      String poolName, int poolSize, ThreadFactory namedThreadFactory) {
    return createExecutor(
        poolName, poolSize, poolSize, Duration.ZERO, Integer.MAX_VALUE, namedThreadFactory);
  }

  public ThreadPoolExecutor createCoalescingDelayExecutor(
      String poolName, ThreadFactory namedThreadFactory) {
    Duration delay = coalescingDelay(poolName);
    // No core thread so that execute() always goes through the queue and its delay.
    return createExecutor(
        poolName,
        0 /* corePoolSize */,
        1 /* maxPoolSize */,
        delay.plusMinutes(1) /* keepAliveTime */,
        new CoalescingDelayQueue<>(delay),
        namedThreadFactory);
  }

  public ThreadPoolExecutor createExecutor(
      String poolName,
      int corePoolSize,
      int maxPoolSize,
      Duration keepAliveTime,
      int queueCapacity,
      ThreadFactory namedThreadFactory) {
    return createExecutor(
        poolName,
        corePoolSize,
        maxPoolSize,
        keepAliveTime,
        newQueue(queueCapacity),
        namedThreadFactory);
  }

  public ThreadPoolExecutor createExecutor(
      String poolName,
      int corePoolSize,
      int maxPoolSize,
      Duration keepAliveTime,
      BlockingQueue<Runnable> queue,
      ThreadFactory namedThreadFactory) {
    return createExecutor(
        poolName,
        corePoolSize,
        maxPoolSize,
        keepAliveTime,
        queue,
        namedThreadFactory,
        PlatformThreadPoolExecutor.DEFAULT_QUEUE_OFFER_TIMEOUT,
        true);
  }

  private static BlockingQueue<Runnable> newQueue(int queueCapacity) {
    return queueCapacity <= 0 ? new SynchronousQueue<>() : new LinkedBlockingQueue<>(queueCapacity);
  }

  private ThreadPoolExecutor createExecutor(
      String poolName,
      int corePoolSize,
      int maxPoolSize,
      Duration keepAliveTime,
      BlockingQueue<Runnable> queue,
      ThreadFactory namedThreadFactory,
      Duration queueOfferTimeout,
      boolean addShutdownHook) {
    ThreadPoolExecutor executor =
        new PlatformThreadPoolExecutor(
            corePoolSize,
            maxPoolSize,
            keepAliveTime.getSeconds(),
            TimeUnit.SECONDS,
            queue,
            namedThreadFactory,
            queueOfferTimeout);
    if (!addShutdownHook) {
      return executor;
    }
    shutdownHookHandler.addShutdownHook(
        executor,
        (exec) -> {
          // Do not use the executor directly as it can create strong reference.
          if (exec != null) {
            log.debug("Shutting down thread pool - {}", poolName);
            boolean isTerminated =
                MoreExecutors.shutdownAndAwaitTermination(
                    exec, SHUTDOWN_TIMEOUT_MINUTES, TimeUnit.MINUTES);
            log.debug("Shutdown status for thread pool- {} is {}", poolName, isTerminated);
          }
        });
    return executor;
  }
}
