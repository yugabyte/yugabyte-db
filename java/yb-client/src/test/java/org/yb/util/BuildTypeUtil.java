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
package org.yb.util;

import org.yb.client.TestUtils;

import java.util.Map;

/**
 * Utility functions for various build types.
 */
public final class BuildTypeUtil {

  private BuildTypeUtil() {
  }

  public static boolean isTSAN() {
    return TestUtils.getBuildType().equals("tsan");
  }

  public static boolean isASAN() {
    return TestUtils.getBuildType().equals("asan");
  }

  public static boolean isRelease() {
    return TestUtils.getBuildType().equals("release");
  }

  public static String getSanitizerOptionsEnvVarName() {
    if (isASAN()) {
      return "ASAN_OPTIONS";
    }
    if (isTSAN()) {
      return "TSAN_OPTIONS";
    }
    return null;
  }

  public static int nonSanitizerVsSanitizer(int nonSanitizerValue, int sanitizerValue) {
    return isSanitizerBuild() ? sanitizerValue : nonSanitizerValue;
  }

  public static long nonTsanVsTsan(long nonTsanValue, long tsanValue) {
    return isTSAN() ? tsanValue : nonTsanValue;
  }

  public static int nonTsanVsTsan(int nonTsanValue, int tsanValue) {
    return isTSAN() ? tsanValue : nonTsanValue;
  }

  /** @return a timeout multiplier to apply in tests based on the build type */
  public static double getTimeoutMultiplier() {
    if (isRelease())
      return 1;
    if (isTSAN())
      return 3;
    if (isASAN())
      return 1.5;
    return 1.5;
  }

  public static long adjustTimeout(long timeout) {
    return (long) (timeout * getTimeoutMultiplier());
  }

  /**
   * Adds the given value to the appropriate sanitizer environment variable (ASAN_OPTIONS or
   * TSAN_OPTIONS). The environment variables are stored and modified in the given map. No actual
   * system environment variables are modified here, unless {@link System#getenv()} is provided as
   * {@code envVars}.
   *
   * @param envVars the map to modify
   * @param whatToAppend what to append to the environment variable
   */
  public static void addToSanitizerOptions(
      Map<String, String> envVars,
      String whatToAppend) {
    String envVarName = getSanitizerOptionsEnvVarName();
    if (envVarName == null)
      return;
    String existingValue = envVars.getOrDefault(
        envVarName, System.getenv().getOrDefault(envVarName, ""));
    envVars.put(envVarName, existingValue.trim() + " " + whatToAppend.trim());
  }

  public static boolean isSanitizerBuild() {
    return isASAN() || isTSAN();
  }
}
