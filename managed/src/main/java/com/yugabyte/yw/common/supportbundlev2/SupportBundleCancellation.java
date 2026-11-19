// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundlev2;

import com.yugabyte.yw.common.ShellResponse;
import java.util.concurrent.CancellationException;
import lombok.extern.slf4j.Slf4j;

/**
 * Recognises a task abort in what the transports hand back. Neither keeps the interrupt flag: ssh
 * returns {@code ERROR_CODE_EXECUTION_CANCELLED}, node agent wraps the {@code
 * InterruptedException}.
 */
@Slf4j
public final class SupportBundleCancellation {

  private SupportBundleCancellation() {}

  public static boolean isCancellation(Throwable throwable) {
    for (Throwable cause = throwable; cause != null; cause = cause.getCause()) {
      if (cause instanceof InterruptedException || cause instanceof CancellationException) {
        return true;
      }
      if (cause.getCause() == cause) {
        break;
      }
    }
    return false;
  }

  /** A cancelled command becomes an exception, so a collection loop can't take it for output. */
  public static ShellResponse throwIfCancelled(ShellResponse response) {
    if (response != null && response.code == ShellResponse.ERROR_CODE_EXECUTION_CANCELLED) {
      throw new CancellationException("Support bundle command cancelled: " + response.message);
    }
    return response;
  }

  /** For the top of a loop: an interrupt that arrived during local work stops the next step. */
  public static void throwIfInterrupted() {
    if (Thread.currentThread().isInterrupted()) {
      throw new CancellationException("Support bundle work interrupted");
    }
  }

  /**
   * Runs cleanup with the interrupt flag cleared, then restores it. On an interrupted thread every
   * remote command is cancelled before it starts, so the cleanup would do nothing.
   */
  public static void runIgnoringInterrupt(Runnable cleanup) {
    boolean interrupted = Thread.interrupted();
    try {
      cleanup.run();
    } finally {
      if (interrupted) {
        Thread.currentThread().interrupt();
      }
    }
  }

  /**
   * As {@link #rethrowIfCancelled(Throwable)}, first logging what the abort stopped. At INFO, not
   * as the caller's error: a cancelled spec didn't fail.
   */
  public static void rethrowIfCancelled(Throwable throwable, String stoppedAt) {
    if (isCancellation(throwable)) {
      log.info("Support bundle work cancelled at {}", stoppedAt);
    }
    rethrowIfCancelled(throwable);
  }

  /** Rethrows an abort, restoring the interrupt flag the transport cleared. */
  public static void rethrowIfCancelled(Throwable throwable) {
    if (!isCancellation(throwable)) {
      return;
    }
    Thread.currentThread().interrupt();
    if (throwable instanceof RuntimeException) {
      throw (RuntimeException) throwable;
    }
    if (throwable instanceof Error) {
      throw (Error) throwable;
    }
    CancellationException cancellation = new CancellationException(throwable.getMessage());
    cancellation.initCause(throwable);
    throw cancellation;
  }
}
