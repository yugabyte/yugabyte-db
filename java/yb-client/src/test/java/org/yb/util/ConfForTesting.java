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

public final class ConfForTesting {

  private static boolean detectCI() {
    boolean isJenkins = System.getProperty("user.name").equals("jenkins") &&
                        System.getenv("BUILD_ID") != null &&
                        System.getenv("JOB_NAME") != null;
    // TODO: also detect other CI environments.
    return isJenkins;
  }

  private static final boolean isCI = detectCI();

  public static boolean isCI() {
    return isCI;
  }

  private ConfForTesting() {
  }

  public static final String DELETE_SUCCESSFUL_LOGS_ENV_VAR =
      "YB_DELETE_SUCCESSFUL_PER_TEST_METHOD_LOGS";

  public static final String GZIP_PER_TEST_METHOD_LOGS_ENV_VAR =
      "YB_GZIP_PER_TEST_METHOD_LOGS";

  public static boolean usePerTestLogFiles() {
    // Don't create per-test-method logs if we're told only one test method is invoked at a time.
    return EnvAndSysPropertyUtil.isEnvVarOrSystemPropertyTrue("YB_JAVA_PER_TEST_LOG_FILES", false);
  }

  public static boolean gzipPerTestMethodLogs() {
    return EnvAndSysPropertyUtil.isEnvVarOrSystemPropertyTrue(GZIP_PER_TEST_METHOD_LOGS_ENV_VAR,
        false);
  }

  public static boolean deleteSuccessfulPerTestMethodLogs() {
    return EnvAndSysPropertyUtil.isEnvVarOrSystemPropertyTrue(DELETE_SUCCESSFUL_LOGS_ENV_VAR,
        false);
  }

  /**
   * @return true if we are in a mode of collecting the list of all tests methods and parameterized
   *         sub-tests.
   */
  public static boolean onlyCollectingTests() {
    // This will look at YB_COLLECT_TESTS_ONLY env var and at the
    // yb.collect.tests.only system property.
    return EnvAndSysPropertyUtil.isEnvVarOrSystemPropertyTrue("YB_COLLECT_TESTS_ONLY");
  }

  /**
   * @return true if requested to keep all data, including cluster data and core files.
   */
  public static boolean keepData() {
    return EnvAndSysPropertyUtil.isEnvVarOrSystemPropertyTrue("YB_JAVATEST_KEEPDATA");
  }

}
