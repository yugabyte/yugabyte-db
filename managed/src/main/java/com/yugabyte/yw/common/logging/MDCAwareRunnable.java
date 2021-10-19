package com.yugabyte.yw.common.logging;

import java.util.Collections;
import java.util.Map;
import java.util.Optional;

import lombok.NonNull;
import org.slf4j.MDC;

public class MDCAwareRunnable implements Runnable {

  private final Map<String, String> context;
  private final Runnable runnable;

  public MDCAwareRunnable(Map<String, String> context, Runnable runnable) {
    this.context = Optional.ofNullable(context).orElse(Collections.emptyMap());
    this.runnable = runnable;
  }

  public MDCAwareRunnable(Runnable runnable) {
    this.context = getCopyOfContextMap();
    this.runnable = runnable;
  }

  private Map<String, String> getCopyOfContextMap() {
    return Optional.ofNullable(MDC.getCopyOfContextMap()).orElse(Collections.emptyMap());
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
    setMDC(context);

    try {
      runnable.run();
    } finally {
      setMDC(previous);
    }
  }
}
