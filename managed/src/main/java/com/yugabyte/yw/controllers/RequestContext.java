/*
 * Copyright 2023 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.controllers;

import java.util.HashMap;
import java.util.Map;
import java.util.Set;
import java.util.function.Consumer;
import lombok.extern.slf4j.Slf4j;
import play.libs.typedmap.TypedKey;

@Slf4j
public class RequestContext {
  private static final ThreadLocal<Map<TypedKey<?>, Object>> context = new ThreadLocal<>();
  ;

  public static <T> void put(TypedKey<T> key, T value) {
    Map<TypedKey<?>, Object> current = context.get();
    if (current == null) {
      context.set(new HashMap<>());
      log.trace(
          "[" + Thread.currentThread().getName() + "]" + " Created new context for key " + key);
    }
    context.get().put(key, value);
    log.trace(
        "[" + Thread.currentThread().getName() + "]" + " Set key " + key + " to value " + value);
  }

  public static <T> void update(TypedKey<T> key, Consumer<T> value) {
    T currentValue = getIfPresent(key);
    if (currentValue == null) {
      log.trace("[" + Thread.currentThread().getName() + "]" + " Nothing to update for key " + key);
      return;
    }
    value.accept(currentValue);
    log.trace("[" + Thread.currentThread().getName() + "]" + " Updated value for key " + key);
  }

  public static <T> void putIfAbsent(TypedKey<T> key, T value) {
    Map<TypedKey<?>, Object> current = context.get();
    if (current == null) {
      context.set(new HashMap<>());
      log.trace(
          "[" + Thread.currentThread().getName() + "]" + " Created new context for key " + key);
    }
    context.get().putIfAbsent(key, value);
    log.trace(
        "[" + Thread.currentThread().getName() + "]" + " Set key " + key + " to value " + value);
  }

  @SuppressWarnings("unchecked")
  public static <T> T getIfPresent(TypedKey<T> key) {
    Map<TypedKey<?>, Object> current = context.get();
    if (current == null) {
      return null;
    }
    return (T) current.get(key);
  }

  @SuppressWarnings("unchecked")
  public static <T> T get(TypedKey<T> key) {
    Map<TypedKey<?>, Object> current = context.get();
    if (current == null) {
      throw new IllegalStateException("Request context is not defined");
    }
    T result = (T) current.get(key);
    if (result == null) {
      throw new IllegalStateException("Key " + key + " not found in request context");
    }
    return result;
  }

  public static <T> T getOrDefault(TypedKey<T> key, T defaultValue) {
    T value = getIfPresent(key);
    if (value == null) {
      return defaultValue;
    }
    return value;
  }

  public static void clean(Set<TypedKey<?>> keys) {
    Map<TypedKey<?>, Object> current = context.get();
    if (current == null) {
      return;
    }
    keys.forEach(
        key -> {
          current.remove(key);
          log.trace("[" + Thread.currentThread().getName() + "]" + " Removed key " + key);
        });
  }
}
