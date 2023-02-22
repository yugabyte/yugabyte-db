package com.yugabyte.yw.controllers;

import java.util.HashMap;
import java.util.Map;

public class RequestContext {
  private static final ThreadLocal<Map<String, Object>> context = new ThreadLocal<>();;

  public static void put(String name, Object value) {
    Map<String, Object> current = context.get();
    if (current == null) {
      context.set(new HashMap<>());
    }
    context.get().put(name, value);
  }

  public static void putIfAbsent(String name, Object value) {
    Map<String, Object> current = context.get();
    if (current == null) {
      context.set(new HashMap<>());
    }
    context.get().putIfAbsent(name, value);
  }

  public static Object get(String name) {
    Map<String, Object> current = context.get();
    if (current == null) {
      return null;
    }
    return current.get(name);
  }

  public static <T> T getOrDefault(String name, T defaultValue) {
    Object value = get(name);
    if (value == null) {
      return defaultValue;
    }
    return (T) value;
  }

  public static void clean() {
    context.remove();
  }
}
