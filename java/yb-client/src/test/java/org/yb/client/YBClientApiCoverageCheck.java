// Copyright (c) YugabyteDB, Inc.
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
package org.yb.client;

import java.lang.reflect.Method;
import java.lang.reflect.Modifier;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.stream.Collectors;
import org.yb.client.YBClient;
import org.yb.client.YBClientApi;

/**
 * Build-time check that every public instance method on {@link YBClient} is declared on {@link
 * YBClientApi} (and is therefore an {@code @Override}).
 *
 * <p>Invoked by the yb-client Maven build after {@code process-test-classes}; not a Surefire test.
 */
public final class YBClientApiCoverageCheck {

  private YBClientApiCoverageCheck() {}

  public static void main(String[] args) {
    List<String> missingFromApi = new ArrayList<>();
    for (Method method : YBClient.class.getDeclaredMethods()) {
      int modifiers = method.getModifiers();
      if (!Modifier.isPublic(modifiers)
          || Modifier.isStatic(modifiers)
          || method.isSynthetic()
          || method.isBridge()) {
        continue;
      }
      if (!isDeclaredOnApi(method)) {
        missingFromApi.add(formatMethod(method));
      }
    }
    if (!missingFromApi.isEmpty()) {
      // Throw so exec-maven-plugin fails the Maven build with BUILD FAILURE (do not System.exit).
      throw new IllegalStateException(
          "YBClientApiCoverageCheck: public instance methods on YBClient must be declared on"
              + " YBClientApi (add them to the interface and mark the YBClient methods with"
              + " @Override):\n  "
              + String.join("\n  ", missingFromApi));
    }
  }

  private static boolean isDeclaredOnApi(Method method) {
    try {
      // getMethod walks YBClientApi and its superinterfaces (e.g. AutoCloseable.close).
      YBClientApi.class.getMethod(method.getName(), method.getParameterTypes());
      return true;
    } catch (NoSuchMethodException e) {
      return false;
    }
  }

  private static String formatMethod(Method method) {
    String params =
        Arrays.stream(method.getParameterTypes())
            .map(Class::getTypeName)
            .collect(Collectors.joining(", "));
    return method.getReturnType().getTypeName() + " " + method.getName() + "(" + params + ")";
  }
}
