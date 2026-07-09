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
package org.yb;

import org.junit.runners.BlockJUnit4ClassRunner;
import org.junit.runners.model.FrameworkMethod;
import org.junit.runners.model.InitializationError;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.TestUtils;
import org.yb.util.ConfForTesting;
import org.yb.util.TestFilterUtil;

import java.util.Collections;
import java.util.List;
import java.util.stream.Collectors;
import java.util.stream.Stream;
import java.lang.reflect.Modifier;

public class YBTestRunner extends BlockJUnit4ClassRunner {

  private static final Logger LOG = LoggerFactory.getLogger(YBTestRunner.class);

  private Stream<FrameworkMethod> getFilteredChildren() {
    if (TestFilterUtil.shouldSkip(getTestClass().getJavaClass().getAnnotations())) {
      return Stream.empty();
    }
    return super.getChildren().stream()
        .filter(m -> !TestFilterUtil.shouldSkip(m.getAnnotations()));
  }

  public YBTestRunner(Class<?> klass) throws InitializationError {
    super(klass);
    final String specifiedClassName = klass.getName();
    assert !Modifier.isAbstract(klass.getModifiers()) :
           "YBTestRunner constructor invoked for an abstract class " + specifiedClassName;

    if (ConfForTesting.onlyCollectingTests()) {
      getFilteredChildren().forEach(method -> {
        Class declaringClass = method.getDeclaringClass();
        final String declaringClassName = declaringClass.getName();
        final String methodName = method.getMethod().getName();
        if (!declaringClassName.equals(klass.getName())) {
          LOG.info(
              "For test method " + methodName + ": " +
              "declaring class is " + declaringClassName + " (" +
              (Modifier.isAbstract(declaringClass.getModifiers()) ? "" : "not ") +
              "abstract), specified class: " + specifiedClassName +
              ". This is possible with inheritance. Using class: " + specifiedClassName
          );
        }
        TestUtils.reportCollectedTest(specifiedClassName, methodName);
      });
    }
  }

  @Override
  protected List<FrameworkMethod> getChildren() {
    if (ConfForTesting.onlyCollectingTests()) {
      return Collections.emptyList();
    }
    return getFilteredChildren().collect(Collectors.toList());
  }

}
