package com.yugabyte.yw.common.logging;

import akka.dispatch.Batchable;
import akka.dispatch.Dispatcher;
import akka.dispatch.ExecutorServiceFactoryProvider;
import akka.dispatch.MessageDispatcherConfigurator;
import scala.concurrent.duration.Duration;
import scala.concurrent.duration.FiniteDuration;
import java.util.Map;
import org.slf4j.MDC;
import scala.concurrent.ExecutionContext;

/**
 * This custom dispatcher is used to capture MDC, propagate it through an {@link
 * scala.concurrent.ExecutionContext} and restore when the executor finishes. Note that capturing
 * the context in the dispatcher constructor and overriding {@link
 * akka.dispatch.Dispatcher#execute(Runnable)} will result in context being set only once and
 * subsequent runnables on the same thread will no longer be able to access it after it is cleared.
 */
public class MDCPropagatingDispatcher extends Dispatcher {

  class MDCPropagatingExecutionContext implements ExecutionContext {
    private final Map<String, String> context = MDC.getCopyOfContextMap();
    private final MDCPropagatingDispatcher self = MDCPropagatingDispatcher.this;

    @Override
    public void reportFailure(Throwable cause) {
      self.reportFailure(cause);
    }

    @Override
    public void execute(Runnable runnable) {
      Runnable mdcAwareRunnable = new MDCAwareRunnable(context, runnable);

      if (self.batchable(runnable)) {
        self.execute(
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
        self.execute(mdcAwareRunnable);
      }
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

  @Override
  public ExecutionContext prepare() {
    return new MDCPropagatingExecutionContext();
  }
}
