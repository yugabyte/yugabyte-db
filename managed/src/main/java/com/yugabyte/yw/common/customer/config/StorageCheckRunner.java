// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.customer.config;

import com.google.common.util.concurrent.ThreadFactoryBuilder;
import com.yugabyte.yw.common.PlatformExecutorFactory;
import java.time.Duration;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Future;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import javax.inject.Inject;
import javax.inject.Singleton;
import org.apache.pekko.Done;
import org.apache.pekko.actor.CoordinatedShutdown;

/**
 * Runs the storage checks of the ValidateStorageConfig task with a deadline. They block on cloud
 * SDK calls that have no overall deadline of their own, and can't be interrupted reliably: a check
 * that timed out keeps its thread until the SDK call returns. The pool (yb.storage_config_validation)
 * has no queue, so that checks don't time out waiting behind hung ones; when every thread is busy,
 * the check is rejected.
 */
@Singleton
public class StorageCheckRunner {

  public static final String POOL_NAME = "storage_config_validation";

  private final ExecutorService executor;

  @Inject
  public StorageCheckRunner(
      PlatformExecutorFactory executorFactory, CoordinatedShutdown coordinatedShutdown) {
    this.executor =
        executorFactory.createNonBlockingExecutor(
            POOL_NAME,
            new ThreadFactoryBuilder()
                .setNameFormat("storage-config-validation-%d")
                .setDaemon(true)
                .build());
    coordinatedShutdown.addTask(
        CoordinatedShutdown.PhaseServiceStop(),
        "storage-config-validation-shutdown",
        () -> {
          executor.shutdown();
          return CompletableFuture.completedFuture(Done.done());
        });
  }

  /**
   * Runs the check and waits for it up to the timeout. A failure of the check is rethrown as is.
   *
   * @throws RejectedExecutionException when every thread of the pool is busy
   * @throws TimeoutException when the check didn't finish in time
   * @throws InterruptedException when the waiting thread was interrupted
   */
  public void run(Runnable check, Duration timeout)
      throws TimeoutException, InterruptedException {
    Future<?> future = executor.submit(check);
    try {
      future.get(timeout.toMillis(), TimeUnit.MILLISECONDS);
    } catch (TimeoutException | InterruptedException e) {
      future.cancel(true);
      throw e;
    } catch (ExecutionException e) {
      Throwable cause = e.getCause();
      if (cause instanceof RuntimeException) {
        throw (RuntimeException) cause;
      }
      if (cause instanceof Error) {
        throw (Error) cause;
      }
      throw new RuntimeException(cause);
    }
  }
}
