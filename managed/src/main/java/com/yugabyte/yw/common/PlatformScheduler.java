// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import java.time.Duration;
import java.util.concurrent.atomic.AtomicBoolean;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import org.apache.pekko.actor.ActorSystem;
import org.apache.pekko.actor.Cancellable;
import scala.concurrent.ExecutionContext;

/** For easy creation of scheduler that will shutdown on app shutdown. */
@Slf4j
@Singleton
public class PlatformScheduler {
  public static final long SHUTDOWN_WAIT_TIMEOUT_MS = 60000L;
  private final ActorSystem actorSystem;
  private final ExecutionContext executionContext;
  private final ShutdownHookHandler shutdownHookHandler;

  @Inject
  public PlatformScheduler(
      ActorSystem actorSystem,
      ExecutionContext executionContext,
      ShutdownHookHandler shutdownHookHandler) {
    this.actorSystem = actorSystem;
    this.executionContext = executionContext;
    this.shutdownHookHandler = shutdownHookHandler;
  }

  public Cancellable schedule(
      String name, Duration initialDelay, Duration interval, Runnable runnable) {
    final AtomicBoolean isRunning = new AtomicBoolean();
    final Object lock = new Object();

    Cancellable cancellable =
        actorSystem
            .scheduler()
            .scheduleWithFixedDelay(
                initialDelay,
                interval,
                () -> {
                  boolean shouldRun = false;
                  synchronized (lock) {
                    // Synchronized block in shutdown and this should be serialized.
                    shouldRun =
                        !shutdownHookHandler.isShutdown() && isRunning.compareAndSet(false, true);
                  }
                  if (shouldRun) {
                    try {
                      runnable.run();
                    } finally {
                      isRunning.set(false);
                      if (shutdownHookHandler.isShutdown()) {
                        synchronized (lock) {
                          lock.notify();
                        }
                      }
                    }
                  } else {
                    log.warn(
                        "Previous run of scheduler {} is in progress or it is being shut down",
                        name);
                  }
                },
                executionContext);
    shutdownHookHandler.addShutdownHook(
        cancellable,
        can -> {
          // Do not use the cancellable directly as it can create strong reference.
          if (can != null && !can.isCancelled()) {
            log.debug("Shutting down scheduler - {}", name);
            synchronized (lock) {
              while (isRunning.get()) {
                try {
                  lock.wait(SHUTDOWN_WAIT_TIMEOUT_MS);
                } catch (InterruptedException e) {
                  log.debug("Timed out waiting to shut down scheduler - {}", name);
                  break;
                }
              }
            }
            boolean isCancelled = can.cancel();
            log.debug("Shutdown status for scheduler - {} is {}", name, isCancelled);
          }
        });
    return cancellable;
  }
}
