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
import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.TimeUnit;

/**
 * Base class for all YB Java tests.
 */
public class BaseYBTest {
  private static final Logger LOG = LoggerFactory.getLogger(BaseYBTest.class);

  private static String currentTestClassName;
  private static String currentTestMethodName;

  @Rule
  public final Timeout METHOD_TIMEOUT = new Timeout(
      TestUtils.adjustTimeoutForBuildType(getTestMethodTimeoutSec()), TimeUnit.SECONDS);

  /** This allows to expect a particular exception to be thrown in a test. */
  @Rule
  public final ExpectedException thrown = ExpectedException.none();

  @Rule
  public final TestRule watcher = new TestWatcher() {

    private Map<String, Long> startTimesMs = new HashMap<>();

    private final String descriptionToStr(Description description) {
      return TestUtils.getClassAndMethodStr(description);
    }

    @Override
    protected void starting(Description description) {
      currentTestClassName = description.getClassName();
      currentTestMethodName = description.getMethodName();

      if (TestUtils.isEnvVarTrue("YB_JAVA_PER_TEST_METHOD_OUTPUT_FILES")) {
        // An experimental mode with a separate output file for each test method.
        System.out.flush();
        System.err.flush();
        String outputPrefix = TestUtils.getSurefireTestReportPrefix();
        try {
          System.setOut(new PrintStream(new FileOutputStream(outputPrefix + "stdout.txt")));
          System.setErr(new PrintStream(new FileOutputStream(outputPrefix + "stderr.txt")));
        } catch (IOException ex) {
          throw new RuntimeException(ex);
        }
      }

      // The format of the heading below is important as it is parsed by external test dashboarding
      // tools.
      String descStr = descriptionToStr(description);
      TestUtils.printHeading(System.out, "Starting YB Java test: " + descStr);
      startTimesMs.put(descStr, System.currentTimeMillis());
    }

    @Override
    protected void succeeded(Description description) {
      LOG.info("YB Java test succeeded: " + descriptionToStr(description));
      super.succeeded(description);
    }

    @Override
    protected void failed(Throwable e, Description description) {
      LOG.error("YB Java test failed: " + descriptionToStr(description), e);
      super.failed(e, description);
    }

    /**
     * Invoked when a test method finishes (whether passing or failing).
     */
    @Override
    protected void finished(Description description) {
      // The format of the heading below is important as it is parsed by external test dashboarding
      // tools.
      String descStr = TestUtils.getClassAndMethodStr(description);
      Long startTimeMs = startTimesMs.get(descStr);
      if (startTimeMs == null) {
        LOG.error("Could not identify start time for test " + descStr + " to compute its duration");
      } else {
        double elapsedTimeSec = (System.currentTimeMillis() - startTimeMs) / 1000.0;
        LOG.info("YB Java test " + descStr + String.format(" took %.2f seconds", elapsedTimeSec));
      }
      TestUtils.printHeading(System.out, "Finished YB Java test: " + descStr);
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

}
