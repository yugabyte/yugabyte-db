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
package org.kududb.client;

import com.google.common.base.Preconditions;
import org.kududb.annotations.InterfaceAudience;
import org.kududb.annotations.InterfaceStability;

import java.util.ArrayDeque;
import java.util.Queue;

/**
 * Class that helps tracking row errors. All methods are thread-safe.
 */
@InterfaceAudience.Private
@InterfaceStability.Evolving
public class ErrorCollector {
  private final Queue<RowError> errorQueue;
  private final int maxCapacity;
  private boolean overflowed;

  /**
   * Create a new error collector with a maximum capacity.
   * @param maxCapacity how many errors can be stored, has to be higher than 0
   */
  public ErrorCollector(int maxCapacity) {
    Preconditions.checkArgument(maxCapacity > 0, "Need to be able to store at least one row error");
    this.maxCapacity = maxCapacity;
    this.errorQueue = new ArrayDeque<>(maxCapacity);
  }

  /**
   * Add a new error to this collector. If it is already at max capacity, the oldest error will be
   * discarded before the new one is added.
   * @param rowError a row error to collect
   */
  public synchronized void addError(RowError rowError) {
    if (errorQueue.size() >= maxCapacity) {
      errorQueue.poll();
      overflowed = true;
    }
    errorQueue.add(rowError);
  }

  /**
   * Get the current count of collected row errors. Cannot be greater than the max capacity this
   * instance was configured with.
   * @return the count of errors
   */
  public synchronized int countErrors() {
    return errorQueue.size();
  }

  /**
   * Get all the errors that have been collected and an indication if the list overflowed.
   * The list of errors cleared and the overflow state is reset.
   * @return an object that contains both the list of row errors and the overflow status
   */
  public synchronized RowErrorsAndOverflowStatus getErrors() {
    RowError[] returnedErrors = new RowError[errorQueue.size()];
    errorQueue.toArray(returnedErrors);
    errorQueue.clear();

    RowErrorsAndOverflowStatus returnObject =
        new RowErrorsAndOverflowStatus(returnedErrors, overflowed);
    overflowed = false;
    return returnObject;
  }
}
