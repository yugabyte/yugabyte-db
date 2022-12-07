package org.yb.util;

// Copyright (c) YugaByte, Inc.
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

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.Collection;
import java.util.List;
import java.util.Vector;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;

public class MiscUtil {
  private static final Logger LOG = LoggerFactory.getLogger(MiscUtil.class);

  public interface ThrowingCallable<T, E extends Throwable> {
    T call() throws E;
  }

  public static void runInParallel(Collection<ThrowingRunnable> cmds,
                                   CountDownLatch stopLatch,
                                   int completeTimeoutSecs) throws Throwable {
    final List<Throwable> throwable = new Vector<>();
    ExecutorService es = Executors.newFixedThreadPool(cmds.size());
    for (ThrowingRunnable c : cmds) {
      es.submit(() -> {
        try {
          c.run();
        } catch (Throwable t) {
          LOG.error("Error detected ", t);
          throwable.add(t);
        }
      });
    }
    stopLatch.await();
    es.shutdown();
    if (!es.awaitTermination(completeTimeoutSecs, TimeUnit.SECONDS)) {
      throw new RuntimeException("Timed out");
    }
    if (!throwable.isEmpty()) {
      throw throwable.get(0);
    }
  }
}
