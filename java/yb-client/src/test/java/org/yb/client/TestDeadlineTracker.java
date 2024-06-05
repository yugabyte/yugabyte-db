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

import static org.yb.AssertionWrappers.*;

import com.google.common.base.Stopwatch;
import com.google.common.base.Ticker;
import org.junit.Test;

import java.util.concurrent.atomic.AtomicLong;

import org.yb.YBTestRunner;

import org.junit.runner.RunWith;

@RunWith(value=YBTestRunner.class)
public class TestDeadlineTracker {

  @Test
  public void testTimeout() {
    final AtomicLong timeToReturn = new AtomicLong();
    Ticker ticker = new Ticker() {
      @Override
      public long read() {
        return timeToReturn.get();
      }
    };
    Stopwatch stopwatch = Stopwatch.createStarted(ticker);

    // no timeout set
    DeadlineTracker tracker = new DeadlineTracker(stopwatch);
    tracker.setDeadline(0);
    assertFalse(tracker.hasDeadline());
    assertFalse(tracker.timedOut());

    // 500ms timeout set
    tracker.reset();
    tracker.setDeadline(500);
    assertTrue(tracker.hasDeadline());
    assertFalse(tracker.timedOut());
    assertFalse(tracker.wouldSleepingTimeout(499));
    assertTrue(tracker.wouldSleepingTimeout(500));
    assertTrue(tracker.wouldSleepingTimeout(501));
    assertEquals(500, tracker.getMillisBeforeDeadline());

    // fast forward 200ms
    timeToReturn.set(200 * 1000000);
    assertTrue(tracker.hasDeadline());
    assertFalse(tracker.timedOut());
    assertFalse(tracker.wouldSleepingTimeout(299));
    assertTrue(tracker.wouldSleepingTimeout(300));
    assertTrue(tracker.wouldSleepingTimeout(301));
    assertEquals(300, tracker.getMillisBeforeDeadline());

    // fast forward another 400ms, so the RPC timed out
    timeToReturn.set(600 * 1000000);
    assertTrue(tracker.hasDeadline());
    assertTrue(tracker.timedOut());
    assertTrue(tracker.wouldSleepingTimeout(299));
    assertTrue(tracker.wouldSleepingTimeout(300));
    assertTrue(tracker.wouldSleepingTimeout(301));
    assertEquals(1, tracker.getMillisBeforeDeadline());
  }
}
