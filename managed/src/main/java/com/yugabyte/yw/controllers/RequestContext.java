package com.yugabyte.yw.controllers;

import java.util.HashMap;
import java.util.Map;
import play.libs.typedmap.TypedKey;

public class RequestContext {
  private static final ThreadLocal<Map<TypedKey<?>, Object>> context = new ThreadLocal<>();;

  public static <T> void put(TypedKey<T> key, T value) {
    Map<TypedKey<?>, Object> current = context.get();
    if (current == null) {
      context.set(new HashMap<>());
    }
    context.get().put(key, value);
  }

  public static <T> void putIfAbsent(TypedKey<T> key, T value) {
    Map<TypedKey<?>, Object> current = context.get();
    if (current == null) {
      context.set(new HashMap<>());
    }
    context.get().putIfAbsent(key, value);
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

  public static void clean() {
    context.remove();
  }
}
