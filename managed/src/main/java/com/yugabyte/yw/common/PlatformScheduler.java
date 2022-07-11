// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import akka.actor.ActorSystem;
import akka.actor.Cancellable;
import java.time.Duration;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicBoolean;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import play.inject.ApplicationLifecycle;
import scala.concurrent.ExecutionContext;

/** For easy creation of scheduler that will shutdown on app shutdown. */
@Slf4j
@Singleton
public class PlatformScheduler {
  private final ActorSystem actorSystem;
  private final ExecutionContext executionContext;
  private final ApplicationLifecycle lifecycle;
  private final ExecutorService shutdownExecutor;

  @Inject
  public PlatformScheduler(
      ActorSystem actorSystem, ExecutionContext executionContext, ApplicationLifecycle lifecycle) {
    this.actorSystem = actorSystem;
    this.executionContext = executionContext;
    this.lifecycle = lifecycle;
    this.shutdownExecutor = Executors.newCachedThreadPool();
  }

  public Cancellable schedule(
      String name, Duration initialDelay, Duration interval, Runnable runnable) {
    AtomicBoolean isRunning = new AtomicBoolean();
    Cancellable cancellable =
        actorSystem
            .scheduler()
            .schedule(
                initialDelay,
                interval,
                () -> {
                  if (isRunning.compareAndSet(false, true)) {
                    try {
                      runnable.run();
                    } finally {
                      isRunning.set(false);
                    }
                  } else {
                    log.warn("Previous run of scheduler {} is in progress", name);
                  }
                },
                executionContext);
    lifecycle.addStopHook(
        () -> {
          if (cancellable.isCancelled()) {
            return CompletableFuture.completedFuture(true);
          }
          return CompletableFuture.supplyAsync(
              () -> {
                log.debug("Shutting down scheduler - {}", name);
                return cancellable.cancel();
              },
              shutdownExecutor);
        });
    return cancellable;
  }
}
