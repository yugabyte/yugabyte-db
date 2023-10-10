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
import java.util.concurrent.ExecutorService;
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

  public static final int SHUTDOWN_TIMEOUT_MINUTES = 5;

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
        poolName, corePoolSize, maxPoolSize, Duration.ZERO, Integer.MAX_VALUE, namedThreadFactory);
  }

  public ExecutorService createFixedExecutor(
      String poolName, int poolSize, ThreadFactory namedThreadFactory) {
    return createExecutor(
        poolName, poolSize, poolSize, Duration.ZERO, Integer.MAX_VALUE, namedThreadFactory);
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
            queueCapacity <= 0
                ? new SynchronousQueue<>()
                : new LinkedBlockingQueue<>(queueCapacity),
            namedThreadFactory);
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
