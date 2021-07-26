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

import org.junit.runner.Description;
import org.junit.runner.Runner;
import org.junit.runner.manipulation.Filter;
import org.junit.runners.Parameterized;
import org.junit.runners.parameterized.BlockJUnit4ClassRunnerWithParameters;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.TestUtils;
import org.yb.util.ConfForTesting;

import java.util.Collections;
import java.util.List;

public class YBParameterizedTestRunner extends Parameterized {
  /**
   * Only called reflectively. Do not use programmatically.
   */
  private static final Logger LOG = LoggerFactory.getLogger(YBParameterizedTestRunner.class);

  protected boolean shouldRunTests() {
    return true;
  }

  public YBParameterizedTestRunner(Class<?> klass) throws Throwable {
    super(klass);

    if (!shouldRunTests()) {
      return;
    }
    if (ConfForTesting.onlyCollectingTests()) {
      for (Runner runner : super.getChildren()) {
        BlockJUnit4ClassRunnerWithParameters r = ((BlockJUnit4ClassRunnerWithParameters) runner);
        r.filter(new Filter() {
          @Override
          public boolean shouldRun(Description description) {
            TestUtils.reportCollectedTest(description.getClassName(), description.getMethodName());
            // To avoid NoTestsRemainException.
            return true;
          }

          @Override
          public String describe() {
            return "<none>";
          }
        });
      }
    }
  }

  @Override
  protected List<Runner> getChildren() {
    if (ConfForTesting.onlyCollectingTests() || !shouldRunTests()) {
      return Collections.emptyList();
    }
    return super.getChildren();
  }
}
