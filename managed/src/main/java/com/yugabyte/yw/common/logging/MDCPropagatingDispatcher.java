// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.logging;

import java.util.Map;
import org.apache.pekko.dispatch.Batchable;
import org.apache.pekko.dispatch.Dispatcher;
import org.apache.pekko.dispatch.ExecutorServiceFactoryProvider;
import org.apache.pekko.dispatch.MessageDispatcherConfigurator;
import org.slf4j.MDC;
import scala.concurrent.ExecutionContext;
import scala.concurrent.duration.Duration;
import scala.concurrent.duration.FiniteDuration;

/**
 * This custom dispatcher is used to capture MDC, propagate it through an {@link
 * scala.concurrent.ExecutionContext} and restore when the executor finishes. Note that capturing
 * the context in the dispatcher constructor and overriding {@link
 * org.apache.pekko.dispatch.Dispatcher#execute(Runnable)} will result in context being set only
 * once and subsequent runnables on the same thread will no longer be able to access it after it is
 * cleared.
 */
public class MDCPropagatingDispatcher extends Dispatcher {

  class MDCPropagatingExecutionContext implements ExecutionContext {
    private final Map<String, String> context = MDC.getCopyOfContextMap();
    private final MDCPropagatingDispatcher dispatcher;

    private MDCPropagatingExecutionContext() {
      this.dispatcher = MDCPropagatingDispatcher.this;
    }

    @Override
    public void reportFailure(Throwable cause) {
      dispatcher.reportFailure(cause);
    }

    @Override
    public void execute(Runnable runnable) {
      dispatcher.execute(context, runnable);
    }
  }

  public MDCPropagatingDispatcher(
      MessageDispatcherConfigurator _configurator,
      String id,
      int throughput,
      Duration throughputDeadlineTime,
      ExecutorServiceFactoryProvider executorServiceFactoryProvider,
      FiniteDuration shutdownTimeout) {
    super(
        _configurator,
        id,
        throughput,
        throughputDeadlineTime,
        executorServiceFactoryProvider,
        shutdownTimeout);
  }

  private void execute(Map<String, String> context, Runnable runnable) {
    Runnable mdcAwareRunnable = new MDCAwareRunnable(context, runnable);
    if (super.batchable(runnable)) {
      super.execute(
          new Batchable() {
            @Override
            public boolean isBatchable() {
              return true;
            }

            @Override
            public void run() {
              mdcAwareRunnable.run();
            }
          });
    } else {
      super.execute(mdcAwareRunnable);
    }
  }

  @Override
  public void execute(Runnable runnable) {
    // Dispatcher also implements ExecutionContext that may be injected.
    // This forces MDCPropagatingExecutionContext to be used for MDC awareness.
    prepare().execute(runnable);
  }

  @Override
  public ExecutionContext prepare() {
    return new MDCPropagatingExecutionContext();
  }
}
