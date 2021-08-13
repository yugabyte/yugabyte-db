// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import akka.actor.ActorSystem;
import com.google.common.annotations.VisibleForTesting;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.CallHomeManager;
import com.yugabyte.yw.models.Customer;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.Environment;
import scala.concurrent.ExecutionContext;
import scala.concurrent.duration.Duration;

@Singleton
public class CallHome {

  public static final Logger LOG = LoggerFactory.getLogger(CallHome.class);

  private final ActorSystem actorSystem;

  private final ExecutionContext executionContext;

  private CallHomeManager callHomeManager;

  private final Environment environment;

  // Interval at which to send callhome diagnostics in minutes
  private final int YB_CALLHOME_INTERVAL = 60;

  private AtomicBoolean running = new AtomicBoolean(false);

  @Inject
  public CallHome(
      ActorSystem actorSystem,
      ExecutionContext executionContext,
      CallHomeManager callHomeManager,
      Environment environment) {
    this.actorSystem = actorSystem;
    this.executionContext = executionContext;
    this.environment = environment;
    this.callHomeManager = callHomeManager;

    // We don't want to start callhome on dev environments
    if (this.environment.isDev()) {
      LOG.info("Skip callhome scheduling");
    } else {
      LOG.info("Initialize callhome service");
      this.initialize();
    }
  }

  private void initialize() {
    this.actorSystem
        .scheduler()
        .schedule(
            Duration.create(0, TimeUnit.MINUTES), // initialDelay
            Duration.create(YB_CALLHOME_INTERVAL, TimeUnit.MINUTES), // interval
            () -> scheduleRunner(),
            this.executionContext);
  }

  @VisibleForTesting
  void scheduleRunner() {
    if (running.get()) {
      LOG.info("Previous scheduler still running");
      return;
    }

    LOG.info("Running scheduler");
    running.set(true);
    for (Customer c : Customer.getAll()) {
      try {
        callHomeManager.sendDiagnostics(c);
      } catch (Exception e) {
        LOG.error("Error sending callhome for customer: " + c.uuid, e);
      }
    }
    running.set(false);
  }
}
