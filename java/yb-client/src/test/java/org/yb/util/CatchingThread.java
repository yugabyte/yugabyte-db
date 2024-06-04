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
package org.yb.util;

import java.util.concurrent.TimeoutException;
import java.util.concurrent.atomic.AtomicReference;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * Thread that is able to propagate uncaught exceptions. Suppresses {@link InterruptedException}.
 * <p>
 * Should be used thorugh {@link start()} and {@link finish()} methods.
 * <p>
 * (Test would only fail upon {@link finish()} call, not asynchronously.)
 */
// TODO(alex): Use it throughout our codebase.
public class CatchingThread extends Thread {
  private static final Logger LOG = LoggerFactory.getLogger(CatchingThread.class);

  private final ThrowingRunnable runnable;

  private final AtomicReference<Throwable> threadFailure = new AtomicReference<>(null);

  public CatchingThread(String name, ThrowingRunnable runnable) {
    super(name);
    this.runnable = runnable;
  }

  @Override
  public void run() {
    try {
      runnable.run();
    } catch (InterruptedException ex) {
      // NOOP, expected.
    } catch (Throwable th) {
      // Anything not caught is interpreted as a failure.
      LOG.error(getName() + " thread failed!", th);
      threadFailure.set(th);
    }
  }

  /**
   * Interrupt a thread and wait for it to finish.
   * <p>
   * If an exception was caught during thread run, re-throw it.
   */
  public void finish() throws Exception {
    finish(0);
  }

  /**
   * Interrupt a thread and wait for it to finish, throwing {@link TimeoutException} if it didn't
   * terminate in time.
   * <p>
   * If an exception was caught during thread run, re-throw it.
   */
  public void finish(long millis) throws Exception {
    interrupt();
    join(millis);

    if (isAlive()) {
      throw new TimeoutException(getName() + " thread is still alive after " + millis + " ms!");
    }

    rethrowIfCaught();
  }

  /**
   * If an exception was caught during thread run, re-throw it without waiting for the thread to
   * finish.
   */
  public void rethrowIfCaught() throws Exception {
    Throwable th = threadFailure.get();
    if (th == null) {
      return;
    }

    // We expect Throwable to be either Exception or Error (and Error is unchecked), no reason to
    // make people handle Throwable as a whole.
    if (th instanceof Error) {
      throw (Error) th;
    }
    throw (Exception) th;
  }
}
