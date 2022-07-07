// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.logging;

import com.google.common.collect.Maps;
import java.util.Map;
import java.util.Optional;
import java.util.UUID;
import lombok.NonNull;
import org.slf4j.MDC;

public class MDCAwareRunnable implements Runnable {

  private final Map<String, String> context;
  private final Runnable runnable;

  public MDCAwareRunnable(Map<String, String> context, Runnable runnable) {
    this.context = Optional.ofNullable(context).orElse(Maps.newHashMap());
    this.runnable = runnable;
  }

  public MDCAwareRunnable(Runnable runnable) {
    this.context = getCopyOfContextMap();
    this.runnable = runnable;
  }

  private Map<String, String> getCopyOfContextMap() {
    return Optional.ofNullable(MDC.getCopyOfContextMap()).orElse(Maps.newHashMap());
  }

  private void setMDC(@NonNull Map<String, String> context) {
    if (context.isEmpty()) {
      MDC.clear();
    } else {
      MDC.setContextMap(context);
    }
  }

  public void run() {
    Map<String, String> previous = getCopyOfContextMap();
    // Insert correlation-id into the MDC to trace the internal calls given the MDC
    // doesn't already have correlation-id. The old MDC context is later restored.
    context.computeIfAbsent(LogUtil.CORRELATION_ID, k -> UUID.randomUUID().toString());
    setMDC(context);
    try {
      runnable.run();
    } finally {
      setMDC(previous);
    }
  }
}
