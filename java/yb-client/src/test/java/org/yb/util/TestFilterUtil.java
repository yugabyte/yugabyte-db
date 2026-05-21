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
 *
 */
package org.yb.util;

import org.junit.runner.Description;
import org.junit.runner.manipulation.Filter;
import org.yb.client.TestUtils;

import java.lang.annotation.Annotation;

/**
 * Shared filtering logic for test annotations. Used by both YBTestRunner and
 * YBParameterizedTestRunner to skip tests based on build type and platform.
 */
public final class TestFilterUtil {

  private TestFilterUtil() {
  }

  public static boolean shouldSkip(Annotation[] annotations) {
    for (Annotation a : annotations) {
      if ((a instanceof SkipOnTSAN && BuildTypeUtil.isTSAN()) ||
          (a instanceof RequiresLinux && !SystemUtil.IS_LINUX) ||
          (a instanceof RequiresReleaseBuild && !TestUtils.isReleaseBuild())) {
        return true;
      }
    }
    return false;
  }

  /**
   * A JUnit Filter that excludes test methods based on their annotations.
   * Used by YBParameterizedTestRunner where children are Runners, not FrameworkMethods.
   */
  public static final Filter METHOD_FILTER = new Filter() {
    @Override
    public boolean shouldRun(Description description) {
      return !shouldSkip(description.getAnnotations().toArray(new Annotation[0]));
    }

    @Override
    public String describe() {
      return "annotation-based method filter";
    }
  };
}
