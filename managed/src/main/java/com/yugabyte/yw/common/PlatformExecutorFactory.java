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

import com.google.common.util.concurrent.MoreExecutors;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.typesafe.config.Config;
import java.time.Duration;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.ThreadFactory;
import java.util.concurrent.ThreadPoolExecutor;
import java.util.concurrent.TimeUnit;
import lombok.extern.slf4j.Slf4j;
import play.inject.ApplicationLifecycle;

/** For easy and configurable creation of executor that will shutdown on app shutdown. */
@Slf4j
@Singleton
public class PlatformExecutorFactory {

  public static final int SHUTDOWN_TIMEOUT_MINUTES = 5;

  private final Config config;
  private final ApplicationLifecycle lifecycle;
  private final ExecutorService shutdownExecutor;

  @Inject
  public PlatformExecutorFactory(Config config, ApplicationLifecycle lifecycle) {
    this.config = config;
    this.lifecycle = lifecycle;
    this.shutdownExecutor = Executors.newCachedThreadPool();
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

  private String getPath(String poolName, String confKey) {
    return "yb." + poolName + confKey;
  }

  public ExecutorService createExecutor(String configPoolName, ThreadFactory namedThreadFactory) {
    return createExecutor(
        configPoolName,
        ybCorePoolSize(configPoolName),
        ybMaxPoolSize(configPoolName),
        Duration.ofSeconds(keepAliveDuration(configPoolName).getSeconds()),
        ybQueueCapacity(configPoolName),
        namedThreadFactory);
  }

  public ExecutorService createExecutor(
      String poolName, int corePoolSize, int maxPoolSize, ThreadFactory namedThreadFactory) {
    return createExecutor(
        poolName, corePoolSize, maxPoolSize, Duration.ZERO, 0, namedThreadFactory);
  }

  public ExecutorService createFixedExecutor(
      String poolName, int poolSize, ThreadFactory namedThreadFactory) {
    return createExecutor(poolName, poolSize, poolSize, Duration.ZERO, 0, namedThreadFactory);
  }

  public ExecutorService createFixedExecutor(
      String poolName, int poolSize, int queueCapacity, ThreadFactory namedThreadFactory) {
    return createExecutor(
        poolName, poolSize, poolSize, Duration.ZERO, queueCapacity, namedThreadFactory);
  }

  public ExecutorService createExecutor(
      String poolName,
      int corePoolSize,
      int maxPoolSize,
      Duration keepAliveTime,
      int queueCapacity,
      ThreadFactory namedThreadFactory) {
    ThreadPoolExecutor executor =
        new PlatformThreadPoolExecutor(
            corePoolSize,
            maxPoolSize,
            keepAliveTime.getSeconds(),
            TimeUnit.SECONDS,
            new LinkedBlockingQueue<>(queueCapacity == 0 ? Integer.MAX_VALUE : queueCapacity),
            namedThreadFactory);
    lifecycle.addStopHook(
        () -> {
          if (executor.isTerminated()) {
            return CompletableFuture.completedFuture(true);
          }
          return CompletableFuture.supplyAsync(
              () -> {
                log.debug("Shutting down thread pool - {}", poolName);
                return MoreExecutors.shutdownAndAwaitTermination(
                    executor, SHUTDOWN_TIMEOUT_MINUTES, TimeUnit.MINUTES);
              },
              shutdownExecutor);
        });

    return executor;
  }
}
