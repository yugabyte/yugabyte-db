// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//
package org.yb.client;

import com.google.common.base.Stopwatch;
import static java.util.concurrent.TimeUnit.MILLISECONDS;

/**
 * This is a wrapper class around {@link com.google.common.base.Stopwatch} used to track a relative
 * deadline in the future.
 * <p>
 * The watch starts as soon as this object is created with a deadline of 0,
 * meaning that there's no deadline.
 * The deadline has been reached once the stopwatch's elapsed time is equal or greater than the
 * provided deadline.
 */
public class DeadlineTracker {
  private final Stopwatch stopwatch;
  /** relative deadline in milliseconds **/
  private long deadline = 0;

  /**
   * Creates a new tracker, which starts the stopwatch right now.
   */
  public DeadlineTracker() {
    this(Stopwatch.createStarted());
  }

  /**
   * Creates a new tracker, using the specified stopwatch, and starts it right now.
   * The stopwatch is reset if it was already running.
   * @param stopwatch Specific Stopwatch to use
   */
  public DeadlineTracker(Stopwatch stopwatch) {
    if (stopwatch.isRunning()) {
      stopwatch.reset();
    }
    this.stopwatch = stopwatch.start();
  }

  /**
   * Check if we're already past the deadline.
   * @return true if we're past the deadline, otherwise false. Also returns false if no deadline
   * was specified
   */
  public boolean timedOut() {
    if (!hasDeadline()) {
      return false;
    }
    return deadline - stopwatch.elapsed(MILLISECONDS) <= 0;
  }

  /**
   * Get the number of milliseconds before the deadline is reached.
   * <p>
   * This method is used to pass down the remaining deadline to the RPCs, so has special semantics.
   * A deadline of 0 is used to indicate an infinite deadline, and negative deadlines are invalid.
   * Thus, if the deadline has passed (i.e. <tt>deadline - stopwatch.elapsed(MILLISECONDS)
   * &lt;= 0</tt>), the returned value is floored at <tt>1</tt>.
   * <p>
   * Callers who care about this behavior should first check {@link #timedOut()}.
   *
   * @return the remaining millis before the deadline is reached, or 1 if the remaining time is
   * lesser or equal to 0, or Long.MAX_VALUE if no deadline was specified (in which case it
   * should never be called).
   * @throws IllegalStateException if this method is called and no deadline was set
   */
  public long getMillisBeforeDeadline() {
    if (!hasDeadline()) {
      throw new IllegalStateException("This tracker doesn't have a deadline set so it cannot " +
          "answer getMillisBeforeDeadline()");
    }
    long millisBeforeDeadline = deadline - stopwatch.elapsed(MILLISECONDS);
    millisBeforeDeadline = millisBeforeDeadline <= 0 ? 1 : millisBeforeDeadline;
    return millisBeforeDeadline;
  }

  public long getElapsedMillis() {
    return this.stopwatch.elapsed(MILLISECONDS);
  }

  /**
   * Tells if a non-zero deadline was set.
   * @return true if the deadline is greater than 0, false otherwise.
   */
  public boolean hasDeadline() {
    return deadline != 0;
  }

  /**
   * Utility method to check if sleeping for a specified amount of time would put us past the
   * deadline.
   * @param plannedSleepTime number of milliseconds for a planned sleep
   * @return if the planned sleeps goes past the deadline.
   */
  public boolean wouldSleepingTimeout(long plannedSleepTime) {
    if (!hasDeadline()) {
      return false;
    }
    return getMillisBeforeDeadline() - plannedSleepTime <= 0;
  }

  /**
   * Sets the deadline to 0 (no deadline) and restarts the stopwatch from scratch.
   */
  public void reset() {
    deadline = 0;
    stopwatch.reset();
    stopwatch.start();
  }

  /**
   * Get the deadline (in milliseconds).
   * @return the current deadline
   */
  public long getDeadline() {
    return deadline;
  }

  /**
   * Set a new deadline for this tracker. It cannot be smaller than 0,
   * and if it is 0 then it means that there is no deadline (which is the default behavior).
   * This method won't call reset().
   * @param deadline a number of milliseconds greater or equal to 0
   * @throws IllegalArgumentException if the deadline is lesser than 0
   */
  public void setDeadline(long deadline) {
    if (deadline < 0) {
      throw new IllegalArgumentException("The deadline must be greater or equal to 0, " +
          "the passed value is " + deadline);
    }
    this.deadline = deadline;
  }

  public String toString() {
    StringBuffer buf = new StringBuffer("DeadlineTracker(timeout=");
    buf.append(deadline);
    buf.append(", elapsed=").append(stopwatch.elapsed(MILLISECONDS));
    buf.append(")");
    return buf.toString();
  }
}
