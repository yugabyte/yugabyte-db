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
 *
 */
package org.yb;

import org.apache.log4j.Appender;
import org.apache.log4j.ConsoleAppender;
import org.apache.log4j.LogManager;
import org.junit.Rule;
import org.junit.rules.ExpectedException;
import org.junit.rules.TestRule;
import org.junit.rules.TestWatcher;
import org.junit.rules.Timeout;
import org.junit.runner.Description;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.TestUtils;

import java.io.FileOutputStream;
import java.io.IOException;
import java.io.PrintStream;
import java.util.Enumeration;
import java.util.concurrent.TimeUnit;

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

  private static final int UNDEFINED_TEST_METHOD_INDEX = -1;

  // We put "001", "002", etc. in per-test log files.
  private static int testMethodIndex = UNDEFINED_TEST_METHOD_INDEX;

  @Rule
  public final Timeout METHOD_TIMEOUT = new Timeout(
      TestUtils.adjustTimeoutForBuildType(getTestMethodTimeoutSec()), TimeUnit.SECONDS);

  /** This allows to expect a particular exception to be thrown in a test. */
  @Rule
  public final ExpectedException thrown = ExpectedException.none();

  @Rule
  public final TestRule watcher = new TestWatcher() {

    boolean succeeded;
    boolean failed;
    long startTimeMs;
    Throwable failureException;

    private final String descriptionToStr(Description description) {
      return TestUtils.getClassAndMethodStr(description);
    }

    private void switchLogging(Runnable doLogging, Runnable doSwitching) {
      doLogging.run();

      if (TestUtils.usePerTestLogFiles()) {
        doSwitching.run();

        // Re-create the "out" appender.
        org.apache.log4j.Logger rootLogger = LogManager.getRootLogger();
        Enumeration<Appender> appenders = rootLogger.getAllAppenders();
        int numAppenders = 0;
        while (appenders.hasMoreElements()) {
          Appender appender = appenders.nextElement();
          if (!appender.getName().equals("out")) {
            throw new RuntimeException(
                "Expected to have one appender named 'out' to exist, got " + appender.getName());
          }
          numAppenders++;
        }
        if (numAppenders != 1) {
          throw new RuntimeException(
              "Expected to have one appender named 'out' to exist, got " + numAppenders +
                  " appenders");
        }
        Appender oldOutAppender = LogManager.getRootLogger().getAppender("out");
        rootLogger.removeAllAppenders();

        Appender appender = new ConsoleAppender(oldOutAppender.getLayout());
        appender.setName(oldOutAppender.getName());
        rootLogger.addAppender(appender);

        doLogging.run();
      }
    }

    private void resetState() {
      succeeded = false;
      failed = false;
      startTimeMs = 0;
      failureException = null;
    }

    @Override
    protected void starting(Description description) {
      if (testMethodIndex == UNDEFINED_TEST_METHOD_INDEX) {
        testMethodIndex = 1;
      }
      resetState();
      startTimeMs = System.currentTimeMillis();

      currentTestClassName = description.getClassName();
      currentTestMethodName = description.getMethodName();

      String descStr = descriptionToStr(description);

      switchLogging(
          () -> {
            // The format of the heading below is important as it is parsed by external test
            // dashboarding tools.
            TestUtils.printHeading(System.out, "Starting YB Java test: " + descStr);
          },
          () -> {
            // Use a separate output file for each test method.
            String outputPrefix = TestUtils.getTestReportFilePrefix();
            String stdoutPath = outputPrefix + "stdout.txt";
            String stderrPath = outputPrefix + "stderr.txt";
            LOG.info("Writing stdout for test " + descStr + " to " + stdoutPath);

            System.out.flush();
            System.err.flush();
            try {
              System.setOut(new PrintStream(new FileOutputStream(stdoutPath)));
              System.setErr(new PrintStream(new FileOutputStream(stderrPath)));
            } catch (IOException ex) {
              throw new RuntimeException(ex);
            }
          });
    }

    @Override
    protected void succeeded(Description description) {
      super.succeeded(description);
    }

    @Override
    protected void failed(Throwable e, Description description) {
      failureException = e;
      super.failed(e, description);
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
          () -> {
            if (succeeded) {
              LOG.info("YB Java test succeeded: " + descStr);
            }
            if (failed) {
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
          },
          () -> {
            TestUtils.resetDefaultStdOutAndErr();
          });
      resetState();
      testMethodIndex++;
    }
  };

  /**
   * This can be used for subclasses to override each test method's timeout in milliseconds. This is
   * called from the constructor, so the state of the derived class might not be fully initialized.
   */
  public int getTestMethodTimeoutSec() {
    return 120;
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

  public static int getTestMethodIndex() {
    if (testMethodIndex == UNDEFINED_TEST_METHOD_INDEX) {
      throw new RuntimeException("Test method index within the test class not known");
    }
    return testMethodIndex;
  }

}
