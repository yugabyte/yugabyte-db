/**
 * Copyright (c) YugaByte, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
 * in compliance with the License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed under the License
 * is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
 * or implied.  See the License for the specific language governing permissions and limitations
 * under the License.
 */
package org.yb.minicluster;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * Helper runnable that can log what the processes are sending on their stdout and stderr that
 * we'd otherwise miss.
 */
public class LogPrinter {

  private static final Logger LOG = LoggerFactory.getLogger(MiniYBDaemon.class);

  private final String logPrefix;
  private final InputStream stream;
  private final Thread thread;
  private final AtomicBoolean stopRequested = new AtomicBoolean(false);
  private final Object stopper = new Object();
  private final String streamName;

  private boolean stopped = false;

  private static final boolean LOG_PRINTER_DEBUG = false;

  public LogPrinter(String streamName, InputStream stream, String logPrefix) {
    this.streamName = streamName;
    this.stream = stream;

    this.logPrefix = logPrefix;
    this.thread = new Thread(() -> runThread());
    thread.setDaemon(true);
    thread.setName(streamName + " log printer for " + logPrefix.trim());
    thread.start();
  }

  private void runThread() {
    try {
      String line;
      BufferedReader in = new BufferedReader(new InputStreamReader(stream));
      if (LOG_PRINTER_DEBUG) {
        LOG.info("Starting " + streamName + " log printer with prefix " + logPrefix);
      }
      try {
        while (!stopRequested.get()) {
          while ((line = in.readLine()) != null) {
            System.out.println(logPrefix + line);
          }
          // Sleep for a short time and give the child process a chance to generate more output.
          Thread.sleep(10);
        }
      } catch (InterruptedException iex) {
        // This probably means we're stopping, OK to ignore.
      }
      if (LOG_PRINTER_DEBUG) {
        LOG.info("Finished" + streamName + " log printer with prefix " + logPrefix);
      }
      in.close();
    } catch (Exception e) {
      if (!e.getMessage().contains("Stream closed")) {
        LOG.error("Caught error while reading a process' output", e);
      }
    } finally {
      try {
        stream.close();
      } catch (IOException ex) {
        // Ignore, we're stopping anyway.
      }
      synchronized (stopper) {
        stopped = true;
        stopper.notifyAll();
      }
    }
  }

  public void stop() throws InterruptedException {
    stopRequested.set(true);
    thread.interrupt();
    synchronized (stopper) {
      while (!stopped) {
        stopper.wait();
      }
    }
  }
}
