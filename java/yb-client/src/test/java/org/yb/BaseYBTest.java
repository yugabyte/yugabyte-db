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
package org.yb;

import ch.qos.logback.classic.LoggerContext;
import ch.qos.logback.classic.encoder.PatternLayoutEncoder;
import ch.qos.logback.classic.spi.ILoggingEvent;
import ch.qos.logback.core.Appender;
import ch.qos.logback.core.ConsoleAppender;
import com.google.common.base.Preconditions;
import org.junit.Rule;
import org.junit.rules.ExpectedException;
import org.junit.rules.TestRule;
import org.junit.rules.TestWatcher;
import org.junit.rules.Timeout;
import org.junit.runner.Description;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.TestUtils;
import org.yb.util.FileUtil;
import org.yb.util.GzipHelpers;
import org.yb.util.ConfForTesting;
import org.yb.util.Timeouts;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.PrintStream;
import java.util.ArrayList;
import java.util.Enumeration;
import java.util.Iterator;
import java.util.List;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * Base class for all YB Java tests.
 */
public class BaseYBTest {

  private static final Logger LOG = LoggerFactory.getLogger(BaseYBTest.class);

  // Global static variables to keep track of the current test class/method and index (1, 2, 3,
  // etc.) within the test class. We are assuming that we don't not try to run multiple tests in
  // parallel in the same JVM.

  private static String currentTestClassName;
  private static String currentTestMethodName;

  /**
   * A facility for deleting successful per-test-method log files in case no test failures happened.
   */
  static class LogDeleter {
    AtomicBoolean seenTestFailures = new AtomicBoolean(false);
    List<String> logsToDelete = new ArrayList<>();

    public void doCleanup() {
      if (ConfForTesting.deleteSuccessfulPerTestMethodLogs()) {
        if (seenTestFailures.get()) {
          if (!logsToDelete.isEmpty()) {
            LOG.info("Not deleting these test logs, seen test failures: " + logsToDelete);
          }
        } else {
          LOG.info("Deleting test logs (" + ConfForTesting.DELETE_SUCCESSFUL_LOGS_ENV_VAR +
              " is specified, and no test failures seen): " + logsToDelete);
          for (String filePath : logsToDelete) {
            if (new File(filePath).exists() && !new File(filePath).delete()) {
              LOG.warn("Failed to delete: " + filePath);
            }
          }
        }
      }
    }

    public void onTestFailure() {
      seenTestFailures.set(true);
    }

    public void addLogToDelete(String logPath) {
      if (new File(logPath).exists()) {
        logsToDelete.add(logPath);
      }
    }

    public void registerHook() {
      Runtime.getRuntime().addShutdownHook(new Thread(() -> doCleanup()));
    }
  }

  private static LogDeleter logDeleter = new LogDeleter();

  static {
    logDeleter.registerHook();
  }

  @Rule
  public final Timeout METHOD_TIMEOUT = new Timeout(
      TestUtils.finalizeTestTimeoutSec(
          Timeouts.adjustTimeoutSecForBuildType(getTestMethodTimeoutSec())),
      TimeUnit.SECONDS);

  /** This allows to expect a particular exception to be thrown in a test. */
  @Rule
  public final ExpectedException thrown = ExpectedException.none();

  /**
   * Describes actions to be taken when we start/finish running a test within a test class.
   */
  private interface LogSwitcher {
    /**
     * Switches stdout/stderr to a new file if we're in a mode where we want every test within the
     * same test class to use a separate file.
     */
    void switchStandardStreams();

    /**
     * Does common logging. This logging is only done once if we're putting all test output in one
     * file (default mode), and is done twice (to the custom per-test file and to the top-level
     * output file) if we're in the mode where we want every test to use a separate file. This way
     * the information about test success/failure and the time it takes to run a test is present
     * in both the top-level log file and per-test log files.
     */
    void logEventDetails(boolean streamsSwitched);
  }

  @Rule
  public final TestRule watcher = new TestWatcher() {

    boolean succeeded;
    long startTimeMs;
    Throwable failureException;
    String perTestStdoutPath;
    String perTestStderrPath;

    private final String descriptionToStr(Description description) {
      return TestUtils.getClassAndMethodStr(description);
    }

    private void switchLogging(LogSwitcher switcher) {
      switcher.logEventDetails(false);

      if (ConfForTesting.usePerTestLogFiles()) {
        switcher.switchStandardStreams();
        recreateOutAppender();
        switcher.logEventDetails(true);
      }
    }

    private void resetState() {
      succeeded = false;
      startTimeMs = 0;
      failureException = null;
    }

    @Override
    protected void starting(Description description) {
      resetState();
      startTimeMs = System.currentTimeMillis();

      currentTestClassName = description.getClassName();
      currentTestMethodName = description.getMethodName();

      String descStr = descriptionToStr(description);

      switchLogging(new LogSwitcher() {
        @Override
        public void logEventDetails(boolean streamsSwitched) {
          // The format of the heading below is important as it is parsed by external test
          // dashboarding tools.
          TestUtils.printHeading(System.out, "Starting YB Java test: " + descStr);
        }

        @Override
        public void switchStandardStreams() {
          // Use a separate output file for each test method.
          String outputPrefix;
          try {
            outputPrefix = TestUtils.getTestReportFilePrefix();
          } catch (Exception ex) {
            LOG.info("Error when looking up report file prefix for test " + descStr, ex);
            throw ex;
          }
          String stdoutPath = outputPrefix + "stdout.txt";
          String stderrPath = outputPrefix + "stderr.txt";

          perTestStdoutPath = stdoutPath;
          perTestStderrPath = stderrPath;

          LOG.info("Writing stdout for test " + descStr + " to " + stdoutPath);

          System.out.flush();
          System.err.flush();
          try {
            System.setOut(new PrintStream(new FileOutputStream(stdoutPath)));
            System.setErr(new PrintStream(new FileOutputStream(stderrPath)));
          } catch (IOException ex) {
            throw new RuntimeException(ex);
          }
        }
      });
    }

    @Override
    protected void succeeded(Description description) {
      super.succeeded(description);
      succeeded = true;
    }

    @Override
    protected void failed(Throwable e, Description description) {
      failureException = e;
      super.failed(e, description);
      logDeleter.onTestFailure();
    }

    /**
     * Invoked when a test method finishes (whether passing or failing).
     */
    @Override
    protected void finished(Description description) {
      // The format of the heading below is important as it is parsed by external test dashboarding
      // tools.
      final String descStr = descriptionToStr(description);
      switchLogging(
          new LogSwitcher() {
            @Override
            public void logEventDetails(boolean streamsSwitched) {
              if (succeeded) {
                LOG.info("YB Java test succeeded: " + descStr);
              }
              if (failureException != null) {
                LOG.error("YB Java test failed: " + descStr, failureException);
              }
              if (startTimeMs == 0) {
                LOG.error("Could not identify start time for test " + descStr + " to compute its " +
                    "duration");
              } else {
                double elapsedTimeSec = (System.currentTimeMillis() - startTimeMs) / 1000.0;
                LOG.info("YB Java test " + descStr +
                    String.format(" took %.2f seconds", elapsedTimeSec));
              }
              TestUtils.printHeading(System.out, "Finished YB Java test: " + descStr);
              if (streamsSwitched) {
                FileUtil.bestEffortDeleteIfEmpty(perTestStdoutPath);
                FileUtil.bestEffortDeleteIfEmpty(perTestStderrPath);

                if (ConfForTesting.gzipPerTestMethodLogs()) {
                  LOG.info(
                      "Gzipping test logs (" + ConfForTesting.GZIP_PER_TEST_METHOD_LOGS_ENV_VAR +
                          " is set): " + perTestStdoutPath + ", " + perTestStderrPath);
                  perTestStdoutPath = GzipHelpers.bestEffortGzipFile(perTestStdoutPath);
                  perTestStderrPath = GzipHelpers.bestEffortGzipFile(perTestStderrPath);
                }

                if (succeeded && ConfForTesting.deleteSuccessfulPerTestMethodLogs()) {
                  logDeleter.addLogToDelete(perTestStdoutPath);
                  logDeleter.addLogToDelete(perTestStderrPath);
                }
              }
            }

            @Override
            public void switchStandardStreams() {
              TestUtils.resetDefaultStdOutAndErr();
            }
          });
      resetState();
    }

  };

  private void recreateOutAppender() {
    // Re-create the "out" appender.
    ch.qos.logback.classic.Logger rootLogger =
      (ch.qos.logback.classic.Logger) LoggerFactory.getLogger(org.slf4j.Logger.ROOT_LOGGER_NAME);

    int numAppenders = 0;
    for (Iterator<Appender<ILoggingEvent>> i = rootLogger.iteratorForAppenders(); i.hasNext();) {
      ch.qos.logback.core.Appender<ILoggingEvent> appender = i.next();
      numAppenders++;
      if (!appender.getName().equals("out")) {
        throw new RuntimeException(
          "Expected to have one appender named 'out' to exist, got " + appender.getName());
      }
    }
    if (numAppenders != 1) {
      throw new RuntimeException(
        "Expected to have one appender named 'out' to exist, got " + numAppenders +
          " appenders");
    }
    rootLogger.detachAndStopAllAppenders();

    LoggerContext logCtx = (LoggerContext) LoggerFactory.getILoggerFactory();
    PatternLayoutEncoder logEncoder = new PatternLayoutEncoder();
    logEncoder.setContext(logCtx);
    logEncoder.setPattern("%d (%t) [%p - %l] %m%n");
    logEncoder.start();

    ConsoleAppender logConsoleAppender = new ConsoleAppender();
    logConsoleAppender.setContext(logCtx);
    logConsoleAppender.setName("out");
    logConsoleAppender.setEncoder(logEncoder);
    logConsoleAppender.start();
    rootLogger.addAppender(logConsoleAppender);
  }

  /**
   * This can be used for subclasses to override each test method's timeout in milliseconds. This is
   * called from the constructor, so the state of the derived class might not be fully initialized.
   */
  public int getTestMethodTimeoutSec() {
    return 180;
  }

  public static String getCurrentTestClassName() {
    if (currentTestClassName == null) {
      throw new RuntimeException("Current test class name not known");
    }
    return currentTestClassName;
  }

  public static String getCurrentTestMethodName() {
    if (currentTestMethodName == null) {
      throw new RuntimeException("Current test method name not known");
    }
    return currentTestMethodName;
  }

  /**
   * This could be overridden by subclasses to handle
   * @param taskDescription
   */
  protected void handleTaskTimeout(String taskDescription, long timeoutMs, long elapsedMs)
      throws TimeoutException {
    String msg =
        "Task with a timeout of " + timeoutMs + " ms took " + elapsedMs + " ms to complete: " +
            taskDescription;
    TimeoutException ex = new TimeoutException(msg);
    LOG.error(msg, ex);
    throw ex;
  }

  /**
   * Run the given task with the given timeout. This runs the task in a separate thread, so should
   * not be used too frequently.
   *
   * @param timeoutMs timeout in milliseconds
   * @param taskDescription task description to be included in log messages and exception
   * @param runnable the runnable
   * @throws InterruptedException in case the calling thread is interrupted
   * @throws TimeoutException in case the task times out
   */
  protected void runWithTimeout(int timeoutMs, String taskDescription, Runnable runnable)
      throws InterruptedException, TimeoutException {
    Preconditions.checkArgument(timeoutMs > 0, "timeoutMs must be positive");
    Thread taskThread = new Thread(runnable);
    taskThread.setName("Task with " + timeoutMs + " ms timeout: " + taskDescription);
    long startTimeMs = System.currentTimeMillis();
    taskThread.start();

    try {
      taskThread.join(timeoutMs);
    } catch (InterruptedException ex) {
      if (taskThread.isAlive()) {
        taskThread.interrupt();
        // Wait a bit longer for the thread to get interrupted.
        taskThread.join(100);
        if (taskThread.isAlive()) {
          LOG.warn("Task got interrupted and we had trouble joining the thread: " +
              taskDescription);
        }
      }
      throw ex;
    }

    if (!taskThread.isAlive()) {
      // The task completed within the allotted time.
      return;
    }
    long elapsedMs = System.currentTimeMillis() - startTimeMs;
    if (elapsedMs < timeoutMs) {
      LOG.warn("Task with a timeout of " + timeoutMs + " ms completed in " + elapsedMs + " ms " +
               "but the thread was still alive after the completion. This should not happen. " +
               "Calling the timeout handler anyway.");
    }
    handleTaskTimeout(taskDescription, timeoutMs, elapsedMs);
  }

}
