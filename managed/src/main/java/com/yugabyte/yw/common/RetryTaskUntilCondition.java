/*
* Copyright 2022 YugaByte, Inc. and Contributors
*
* Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
* may not use this file except in compliance with the License. You
* may obtain a copy of the License at
*
http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
*/
package com.yugabyte.yw.common;

import java.time.Duration;
import java.util.concurrent.CancellationException;
import java.util.function.Predicate;
import java.util.function.Supplier;

/**
 * This utility helps in retrying a task until a stop condition is met, or a timeout is reached,
 * whichever is earlier.
 */
class RetryTaskUntilCondition<T> {
  private Supplier<T> task;
  private Predicate<T> stopCondition;
  private Duration delayBetweenRetrySecs;
  private Duration timeoutSecs;

  public RetryTaskUntilCondition(
      Supplier<T> task, Predicate<T> stopCondition, long delayBetweenRetrySecs, long timeoutSecs) {
    this.stopCondition = stopCondition;
    this.task = task;
    this.delayBetweenRetrySecs = Duration.ofSeconds(delayBetweenRetrySecs);
    this.timeoutSecs = Duration.ofSeconds(timeoutSecs);
  }

  // Retries the given task and finally returns true if the task stopped due to meeting the given
  // stop condition and not due to a timeout.
  public boolean retryUntilCond() {
    T result = task.get();
    boolean stopRetry = stopCondition.test(result);

    long timeout = System.currentTimeMillis() + timeoutSecs.toMillis();
    while (!stopRetry && System.currentTimeMillis() < timeout) {
      try {
        Thread.sleep(delayBetweenRetrySecs.toMillis());
      } catch (InterruptedException e) {
        throw new CancellationException(e.getMessage());
      }
      result = task.get();
      stopRetry = stopCondition.test(result);
    }

    return !stopRetry;
  }
}
