/**
 * Copyright (c) YugabyteDB, Inc.
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
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicLong;

import org.yb.util.EnvAndSysPropertyUtil;

/**
 * Helper runnable that can log what the processes are sending on their stdout and stderr that
 * we'd otherwise miss.
 */
public class LogPrinter implements AutoCloseable {

  private static final Logger LOG = LoggerFactory.getLogger(MiniYBDaemon.class);

  private static final AtomicLong totalLoggedSize = new AtomicLong();
  private static final AtomicBoolean logSizeExceededThrown = new AtomicBoolean(false);

  private final String logPrefix;
  private final InputStream stream;
  private final Thread thread;
  private final AtomicBoolean stopRequested = new AtomicBoolean(false);
  private final Object stopper = new Object();

  /** A mechanism to wait for a line in the log that says that the server is starting. */
  private final List<LogErrorListener> errorListeners = new ArrayList<>();

  private boolean stopped = false;
  private String errorMessage;

  private static final boolean LOG_PRINTER_DEBUG = false;

  private static final long MAX_ALLOWED_LOGGED_BYTES =
      EnvAndSysPropertyUtil.getLongEnvVarOrSystemProperty(
          "YB_JAVA_TEST_MAX_ALLOWED_LOG_BYTES",
          512 * 1024 * 1024);

  public LogPrinter(InputStream stream, String logPrefix) {
    this(stream, logPrefix, null);
  }

  public LogPrinter(InputStream stream, String logPrefix, LogErrorListener errorListener) {
    this.stream = stream;

    this.logPrefix = logPrefix;
    this.thread = new Thread(() -> runThread());
    if (errorListener != null) {
      addErrorListener(errorListener);
    }

    thread.setDaemon(true);
    thread.setName(logPrinterName());
    thread.start();
  }

  public void addErrorListener(LogErrorListener errorListener) {
    if (stopRequested.get()) {
      return;
    }
    synchronized (errorListeners) {
      this.errorListeners.add(errorListener);
    }
  }

  private String logPrinterName() {
    return "Log printer for '" + logPrefix.trim() + "'";
  }

  /**
   * To be used for logging with prefix.
   * Returns message prefixed with logPrinterName() if called not from log printer thread.
   * No need to have a prefix when called from log printer thread, since thread name is included
   * in log anyway.
   */
  private String withPrefix(String message) {
    if (Thread.currentThread().equals(thread)) {
      return message;
    }
    return logPrinterName() + ": " + message;
  }

  private void runThread() {
    try {
      String line;
      BufferedReader in = new BufferedReader(new InputStreamReader(stream));
      try {
        if (LOG_PRINTER_DEBUG) {
          LOG.info(withPrefix("Starting thread, total log size limit: " +
                   MAX_ALLOWED_LOGGED_BYTES + ", used log size: " + totalLoggedSize.get()));
        }
        try {
          while (!stopRequested.get()) {
            while ((line = in.readLine()) != null) {
              synchronized (errorListeners) {
                for (LogErrorListener l : errorListeners) {
                  l.handleLine(line);
                }
              }
              System.out.println(logPrefix + line);
              if (totalLoggedSize.addAndGet(line.length() + 1) > MAX_ALLOWED_LOGGED_BYTES) {
                if (errorMessage == null) {
                  errorMessage = "Max total log size exceeded: " + MAX_ALLOWED_LOGGED_BYTES;
                  // Show the error once per LogPrinter instance.
                  LOG.warn(withPrefix(errorMessage));
                }

                if (logSizeExceededThrown.compareAndSet(false, true)) {
                  LOG.warn(withPrefix(errorMessage));
                  throw new AssertionError(errorMessage);
                }
                return;
              }
              System.out.flush();
            }
            // Sleep for a short time and give the child process a chance to generate more output.
            Thread.sleep(10);
          }
        } catch (IOException ex) {
          if (ex.getMessage().toLowerCase().contains("stream closed")) {
            // This probably means we're stopping, OK to ignore.
            LOG.info(withPrefix(ex.getMessage()));
          } else {
            throw ex;
          }
        } catch (InterruptedException iex) {
          // This probably means we're stopping, OK to ignore.
          LOG.info(withPrefix(iex.getMessage()));
        } catch (Throwable t) {
          LOG.warn(withPrefix(t.getMessage()), t);
        }
        if (LOG_PRINTER_DEBUG) {
          LOG.info(withPrefix("Finished"));
        }
      } finally {
        if (LOG_PRINTER_DEBUG) {
          LOG.info(withPrefix("Closing input stream"));
        }

        in.close();
      }
    } catch (Exception e) {
      String msg = e.getMessage();
      if (msg == null || !msg.contains("Stream closed")) {
        LOG.error(withPrefix("Caught error while reading a process's output"), e);
      }
    } finally {
      if (LOG_PRINTER_DEBUG) {
        LOG.info(withPrefix("Closing process output stream ..."));
      }

      try {
        stream.close();
      } catch (IOException ex) {
        // Ignore, we're stopping anyway.
      }
      LOG.info(withPrefix("Closed process output stream"));
      synchronized (stopper) {
        stopped = true;
        stopper.notifyAll();
      }
      LOG.info(withPrefix("Finished thread"));
    }
  }

  public void stop() throws InterruptedException {
    LOG.info(withPrefix("Stop requested"));
    stopRequested.set(true);
    thread.interrupt();
    synchronized (stopper) {
      while (!stopped) {
        stopper.wait();
      }
    }

    synchronized (errorListeners) {
      for (LogErrorListener l : errorListeners) {
        l.reportErrorsAtEnd();
      }
    }
  }

  @Override
  public void close() throws Exception {
    stop();
  }

  public String getError() {
    return errorMessage;
  }
}
