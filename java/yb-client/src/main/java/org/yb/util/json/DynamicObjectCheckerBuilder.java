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

package org.yb.util.json;

import java.lang.reflect.InvocationHandler;
import java.lang.reflect.InvocationTargetException;
import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.function.Function;
import java.util.stream.Collectors;

import org.yb.util.Pair;

final class DynamicObjectCheckerBuilder<T extends ObjectCheckerBuilder>
      implements InvocationHandler, ObjectCheckerBuilder {
  private static final List<Pair<Class<?>, Function<Object, Checker>>>
      SPECIAL_ARG_TYPES_HANDLERS = buildSpecialArgTypeHandlers();

  private final Class<T> clazz;
  private final Map<String, String> propertyNames = new HashMap<>();
  private final Map<String, Checker> checkers = new HashMap<>();

  DynamicObjectCheckerBuilder(Class<T> clazz, boolean nullify) {
    this.clazz = clazz;
    for (Method method : clazz.getDeclaredMethods()) {
      propertyNames.put(method.getName(), getPropertyName(method));
    }
    if (nullify) {
      for (String property : propertyNames.values()) {
        checkers.put(property, null);
      }
    }
  }

  @Override
  public Object invoke(
      Object proxy, Method method, Object[] args)
      throws IllegalAccessException, IllegalArgumentException, InvocationTargetException {
    Class<?> declaringClass = method.getDeclaringClass();
    if (declaringClass.equals(ObjectCheckerBuilder.class)) {
      return method.invoke(this, args);
    }
    if (declaringClass.equals(clazz)) {
      if (args.length == 1) {
        checkers.put(propertyNames.get(method.getName()), makeChecker(args[0]));
      } else {
        String nameParameter = String.class.cast(args[0]);
        checkers.put(
            getParameterizedPropertyName(propertyNames.get(method.getName()), nameParameter),
            makeChecker(args[1]));
      }
      return proxy;
    }
    return null;
  }

  @Override
  public ObjectChecker build() {
    return Checkers.object(new HashMap<>(checkers));
  }

  private void ensureMethodIsValid(Method method) {
    String methodName = method.getName();
    if (!method.getReturnType().equals(clazz)) {
      throw new IllegalArgumentException(
          String.format("'%s' must have '%s' return type", methodName, clazz.getName()));
    }
    if (method.getParameterTypes().length > 2) {
      throw new IllegalArgumentException(
          String.format("'%s' must have one or two arguments", methodName));
    }

    Class<?> argType;
    if (method.getParameterTypes().length == 2) {
      if (!method.getParameterTypes()[0].equals(String.class)) {
        throw new IllegalArgumentException(
            String.format("'%s' first argument must be String", methodName));
      }
      argType = method.getParameterTypes()[1];
    } else {
      argType = method.getParameterTypes()[0];
    }

    if (!isAllowedArgType(argType)) {
      throw new IllegalArgumentException(
          String.format(
              "'%s' has wrong argument type '%s', " +
              "only %s or Checker subclass are allowed",
              methodName,
              argType.getName(),
              SPECIAL_ARG_TYPES_HANDLERS.stream()
                  .map(h -> h.getFirst().getSimpleName())
                  .collect(Collectors.joining(", "))));
    }
  }

  private String getPropertyName(Method method) {
    ensureMethodIsValid(method);
    PropertyName name = method.getAnnotation(PropertyName.class);
    return name == null ? buildPropertyName(method.getName()) : name.value();
  }

  private String getParameterizedPropertyName(String propertyName, String parameter) {
    return String.format("%s %s", propertyName, parameter);
  }

  private static String buildPropertyName(String methodName) {
    StringBuilder builder = new StringBuilder();
    for (int i = 0; i < methodName.length(); ++i) {
      char c = methodName.charAt(i);
      if (Character.isUpperCase(c)) {
        builder.append(' ');
      }
      builder.append(i == 0 ? Character.toUpperCase(c) : c);
    }
    return builder.toString();
  }

  private static boolean isAllowedArgType(Class<?> clazz) {
    return Checker.class.isAssignableFrom(clazz) ||
        SPECIAL_ARG_TYPES_HANDLERS.stream().anyMatch(handler -> handler.getFirst().equals(clazz));
  }

  private static Checker makeChecker(Object checkerCandidate) {
    if (checkerCandidate instanceof Checker) {
      return Checker.class.cast(checkerCandidate);
    }
    return SPECIAL_ARG_TYPES_HANDLERS
        .stream()
        .filter(handler -> handler.getFirst().isInstance(checkerCandidate))
        .map(handler -> handler.getSecond().apply(checkerCandidate))
        .findFirst()
        .get();
  }

  private static List<Pair<Class<?>, Function<Object, Checker>>> buildSpecialArgTypeHandlers() {
    List<Pair<Class<?>, Function<Object, Checker>>> result = new ArrayList<>();
    Function<Object, Checker> booleanHandler = obj -> Checkers.equal(Boolean.class.cast(obj));
    result.add(new Pair<>(Boolean.class, booleanHandler));
    result.add(new Pair<>(boolean.class, booleanHandler));
    result.add(new Pair<>(String.class, obj -> Checkers.equal(String.class.cast(obj))));
    result.add(new Pair<>(Checker[].class, obj -> Checkers.array(Checker[].class.cast(obj))));
    return result;
  }
}
