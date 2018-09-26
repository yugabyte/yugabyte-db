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

public class ConfForTesting {

  private static final boolean DEFAULT_USE_PER_TEST_LOG_FILES = true;

  public static final String DELETE_SUCCESSFUL_LOGS_ENV_VAR =
      "YB_DELETE_SUCCESSFUL_PER_TEST_METHOD_LOGS";

  public static final String GZIP_PER_TEST_METHOD_LOGS_ENV_VAR =
      "YB_GZIP_PER_TEST_METHOD_LOGS";

  public static boolean isJenkins() {
    return System.getenv("BUILD_ID") != null && System.getenv("JOB_NAME") != null;
  }

  public static boolean isStringTrue(String value, boolean defaultValue) {
    if (value == null)
      return defaultValue;
    value = value.trim().toLowerCase();
    if (value.isEmpty() || value.equals("auto") || value.equals("default"))
      return defaultValue;
    return !value.equals("0") && !value.equals("no") && !value.equals("false") &&
           !value.equals("off");
  }

  public static boolean isEnvVarTrue(String envVarName, boolean defaultValue) {
    return isStringTrue(System.getenv(envVarName), defaultValue);
  }

  public static boolean isSystemPropertyTrue(String systemPropertyName, boolean defaultValue) {
    return isStringTrue(System.getProperty(systemPropertyName), defaultValue);
  }

  public static boolean usePerTestLogFiles() {
    // Don't create per-test-method logs if we're told only one test method is invoked at a time.
    return isEnvVarTrue("YB_JAVA_PER_TEST_LOG_FILES", false);
  }

  public static boolean gzipPerTestMethodLogs() {
    return isEnvVarTrue(GZIP_PER_TEST_METHOD_LOGS_ENV_VAR, false);
  }

  public static boolean deleteSuccessfulPerTestMethodLogs() {
    return isEnvVarTrue(DELETE_SUCCESSFUL_LOGS_ENV_VAR, false);
  }

  /**
   * @return true if we are in a mode of collecting the list of all tests methods and parameterized
   *         sub-tests.
   */
  public static boolean onlyCollectingTests() {
    return isEnvVarTrue("YB_COLLECT_TESTS_ONLY", false) ||
           isSystemPropertyTrue("yb.only.collect.tests", false);
  }

}
