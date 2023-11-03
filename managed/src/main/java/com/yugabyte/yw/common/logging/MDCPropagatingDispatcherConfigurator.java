package com.yugabyte.yw.common.logging;

import akka.dispatch.DispatcherPrerequisites;
import akka.dispatch.MessageDispatcher;
import akka.dispatch.MessageDispatcherConfigurator;
import com.typesafe.config.Config;
import java.util.concurrent.TimeUnit;
import scala.concurrent.duration.FiniteDuration;

public class MDCPropagatingDispatcherConfigurator extends MessageDispatcherConfigurator {
  public MDCPropagatingDispatcherConfigurator(
      Config _config, DispatcherPrerequisites prerequisites) {
    super(_config, prerequisites);
  }

  @Override
  public MessageDispatcher dispatcher() {
    return new MDCPropagatingDispatcher(
        this,
        config().getString("id"),
        config().getInt("throughput"),
        new FiniteDuration(
            config().getDuration("throughput-deadline-time", TimeUnit.NANOSECONDS),
            TimeUnit.NANOSECONDS),
        configureExecutor(),
        new FiniteDuration(
            config().getDuration("shutdown-timeout", TimeUnit.MILLISECONDS),
            TimeUnit.MILLISECONDS));
  }
}
