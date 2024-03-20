// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.helpers;

import static com.google.common.base.Preconditions.checkNotNull;

import com.google.api.client.util.BackOff;
import com.google.api.client.util.ExponentialBackOff;
import io.ebean.DB;
import io.ebean.Transaction;
import io.ebean.annotation.TxIsolation;
import jakarta.persistence.PersistenceException;
import java.io.IOException;
import lombok.Builder;
import lombok.NonNull;
import lombok.Value;
import lombok.extern.slf4j.Slf4j;

/** DB Transaction util. */
@Slf4j
public final class TransactionUtil {

  private TransactionUtil() {}

  // AttemptCount overrides the time and resets the back-off time when the max elapsed time is hit.
  public static final RetryConfig DEFAULT_RETRY_CONFIG =
      RetryConfig.builder()
          .maxAttemptCount(50)
          .backOff(
              new ExponentialBackOff.Builder()
                  .setInitialIntervalMillis(10)
                  .setMaxElapsedTimeMillis(1000)
                  .setMaxIntervalMillis(100)
                  .setMultiplier(1.2)
                  .build())
          .build();

  /** The retry config on transaction failure. */
  @Value
  @Builder
  public static class RetryConfig {
    private final int maxAttemptCount;
    @NonNull private final BackOff backOff;
  }

  private static boolean isRetryable(PersistenceException e) {
    String errMsg = e.getMessage();
    if (errMsg != null) {
      if (errMsg.contains("could not serialize access due to concurrent update")) {
        return true;
      }
      if (errMsg.contains(
          "could not serialize access due to read/write dependencies among transactions")) {
        return true;
      }
      // This one it thrown by embedded H2 in tests
      if (errMsg.contains("Deadlock detected. The current transaction was rolled back.")) {
        return true;
      }
    }
    return false;
  }

  private static boolean waitIfPossible(RetryConfig config, int attemptCount) {
    if (config == null) {
      return false;
    }
    int maxAttemptCount = config.getMaxAttemptCount();
    // Max attempt count is the hard stop if it is set.
    if (maxAttemptCount > 0 && maxAttemptCount < attemptCount) {
      log.info("maxAttemptCount({}) is reached", maxAttemptCount);
      return false;
    }
    try {
      BackOff backOff = config.getBackOff();
      long waitTime = backOff.nextBackOffMillis();
      if (waitTime == BackOff.STOP) {
        // If max attempt count is set, keep trying with exponential backOff.
        if (attemptCount <= maxAttemptCount) {
          log.debug(
              "Resetting backoff as attemptCount({}) <= maxAttemptCount({}) ",
              attemptCount,
              maxAttemptCount);
          backOff.reset();
          waitTime = backOff.nextBackOffMillis();
        }
      }
      if (waitTime != BackOff.STOP) {
        Thread.sleep(waitTime);
      }
      return true;
    } catch (IOException | InterruptedException e1) {
      log.error("Error occurred", e1);
    }
    return false;
  }

  /**
   * Executes the Runnable instance in transaction.
   *
   * @param runnable the runnable to be invoked.
   * @param config the retry config.
   * @return number of attempts before succeeding.
   */
  public static int doInTxn(Runnable runnable, RetryConfig config) {
    checkNotNull(runnable, "Runnable must be set");
    int attemptCount = 1;
    while (true) {
      try (Transaction transaction = DB.beginTransaction(TxIsolation.SERIALIZABLE)) {
        if (attemptCount > 1) {
          log.info("Retrying({})...", attemptCount);
        } else {
          log.debug("Trying({})...", attemptCount);
        }
        runnable.run();
        transaction.commit();
        break;
      } catch (PersistenceException e) {
        if (isRetryable(e)) {
          // Check if next retry can be attempted.
          if (waitIfPossible(config, attemptCount + 1)) {
            attemptCount++;
            continue;
          }
        }
        throw e;
      }
    }
    return attemptCount;
  }
}
