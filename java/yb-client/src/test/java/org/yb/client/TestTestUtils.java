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

import org.junit.After;
import org.junit.Test;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.atomic.AtomicLong;

import static org.yb.AssertionWrappers.assertEquals;
import static org.yb.AssertionWrappers.assertTrue;

/**
 * Tests for non-trivial helper methods in TestUtils.
 */
import org.yb.YBTestRunner;

import org.junit.runner.RunWith;
import org.yb.util.ProcessUtil;

@RunWith(value=YBTestRunner.class)
public class TestTestUtils {

  public static final Logger LOG = LoggerFactory.getLogger(TestUtils.class);

  private Process proc;

  @After
  public void tearDown() {
    if (proc != null) {
      proc.destroy();
    }
  }

  /**
   * Starts a process that executes the "yes" command (which prints 'y' in a loop),
   * sends a SIGSTOP to the process, and ensures that SIGSTOP does indeed pause the process.
   * Afterwards, sends a SIGCONT to the process and ensures that the process resumes.
   */
  @Test(timeout = 2000)
  public void testPauseAndResume() throws Exception {
    ProcessBuilder processBuilder = new ProcessBuilder("yes");
    proc = processBuilder.start();
    LineCounterRunnable lineCounter = new LineCounterRunnable(proc.getInputStream());
    Thread thread = new Thread(lineCounter);
    thread.setDaemon(true);
    thread.start();
    ProcessUtil.pauseProcess(proc);
    long prevCount;
    do {
      prevCount = lineCounter.getCount();
      Thread.sleep(10);
    } while (prevCount != lineCounter.getCount());
    assertEquals(prevCount, lineCounter.getCount());
    ProcessUtil.resumeProcess(proc);
    do {
      prevCount = lineCounter.getCount();
      Thread.sleep(10);
    } while (prevCount == lineCounter.getCount());
    assertTrue(lineCounter.getCount() > prevCount);
  }

  /**
   * Counts the number of lines in a specified input stream.
   */
  static class LineCounterRunnable implements Runnable {
    private final AtomicLong counter;
    private final InputStream is;

    public LineCounterRunnable(InputStream is) {
      this.is = is;
      counter = new AtomicLong(0);
    }

    @Override
    public void run() {
      BufferedReader in = null;
      try {
        in = new BufferedReader(new InputStreamReader(is));
        while (in.readLine() != null) {
          counter.incrementAndGet();
        }
      } catch (Exception e) {
        LOG.error("Error while reading from the process", e);
      } finally {
        if (in != null) {
          try {
            in.close();
          } catch (IOException e) {
            LOG.error("Error closing the stream", e);
          }
        }
      }
    }

    public long getCount() {
      return counter.get();
    }
  }

  @Test
  public void testDummy() {
    // We run this test just to force Maven to download all test-time dependencies.
  }
}
