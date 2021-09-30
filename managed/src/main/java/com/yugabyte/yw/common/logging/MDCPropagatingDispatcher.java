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

public class MDCPropagatingDispatcher extends Dispatcher {

  private final boolean cloudLoggingEnabled;

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
    cloudLoggingEnabled =
        _configurator.prerequisites().settings().config().getBoolean("yb.cloud.enabled");
  }

  @Override
  public ExecutionContext prepare() {
    if (!cloudLoggingEnabled) {
      return super.prepare();
    }

    return new ExecutionContext() {
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
    };
  }
}
