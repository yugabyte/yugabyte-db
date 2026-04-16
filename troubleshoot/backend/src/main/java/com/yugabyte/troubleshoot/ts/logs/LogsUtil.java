package com.yugabyte.troubleshoot.ts.logs;

import com.google.common.collect.ImmutableMap;
import com.google.common.collect.Maps;
import java.util.Collections;
import java.util.Map;
import java.util.Optional;
import java.util.UUID;
import java.util.concurrent.Callable;
import java.util.function.Supplier;
import org.apache.commons.collections4.MapUtils;
import org.slf4j.MDC;

public class LogsUtil {
  public static final String CORRELATION_ID = "correlation-id";
  public static final String UNIVERSE_ID = "universe-id";

  public static Runnable withUniverseId(Runnable runnable, UUID universeId) {
    return wrapRunnable(runnable, ImmutableMap.of(UNIVERSE_ID, universeId.toString()));
  }

  public static Runnable wrapRunnable(Runnable runnable) {
    return wrapRunnable(runnable, Collections.emptyMap());
  }

  private static Runnable wrapRunnable(Runnable runnable, Map<String, String> mdcValues) {
    Map<String, String> context = MDC.getCopyOfContextMap();
    return () -> {
      setMDC(context);
      mdcValues.forEach(MDC::put);
      try {
        runnable.run();
      } finally {
        setMDC(context);
      }
    };
  }

  public static <T> Callable<T> withUniverseId(Callable<T> callable, UUID universeId) {
    return wrapCallable(callable, ImmutableMap.of(UNIVERSE_ID, universeId.toString()));
  }

  public static <T> Callable<T> wrapCallable(Callable<T> callable) {
    return wrapCallable(callable, Collections.emptyMap());
  }

  private static <T> Callable<T> wrapCallable(Callable<T> callable, Map<String, String> mdcValues) {
    Map<String, String> context = MDC.getCopyOfContextMap();
    return () -> {
      setMDC(context);
      mdcValues.forEach(MDC::put);
      try {
        return callable.call();
      } finally {
        setMDC(context);
      }
    };
  }

  public static void runWithContext(Runnable runnable) {
    Map<String, String> previous = getCopyOfContextMap();
    String correlationId = MDC.get(LogsUtil.CORRELATION_ID);
    if (correlationId == null) {
      correlationId = UUID.randomUUID().toString();
    }
    MDC.put(CORRELATION_ID, correlationId);
    try {
      runnable.run();
    } finally {
      setMDC(previous);
    }
  }

  public static <T> T callWithContext(Supplier<T> function) {
    Map<String, String> previous = getCopyOfContextMap();
    String correlationId = MDC.get(LogsUtil.CORRELATION_ID);
    if (correlationId == null) {
      correlationId = UUID.randomUUID().toString();
    }
    MDC.put(CORRELATION_ID, correlationId);
    try {
      return function.get();
    } finally {
      setMDC(previous);
    }
  }

  private static Map<String, String> getCopyOfContextMap() {
    return Optional.ofNullable(MDC.getCopyOfContextMap()).orElse(Maps.newHashMap());
  }

  private static void setMDC(Map<String, String> context) {
    if (MapUtils.isEmpty(context)) {
      MDC.clear();
    } else {
      MDC.setContextMap(context);
    }
  }
}
