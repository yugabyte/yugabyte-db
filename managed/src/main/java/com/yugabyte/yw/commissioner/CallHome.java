// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import com.google.common.annotations.VisibleForTesting;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.CallHomeManager;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.models.Customer;
import java.time.Duration;
import lombok.extern.slf4j.Slf4j;
import play.Environment;

@Singleton
@Slf4j
public class CallHome {

  private final PlatformScheduler platformScheduler;

  private final CallHomeManager callHomeManager;

  private final Environment environment;

  // Interval at which to send callhome diagnostics in minutes
  private static final int YB_CALLHOME_INTERVAL = 60;

  @Inject
  public CallHome(
      PlatformScheduler platformScheduler,
      CallHomeManager callHomeManager,
      Environment environment) {
    this.platformScheduler = platformScheduler;
    this.environment = environment;
    this.callHomeManager = callHomeManager;
  }

  public void start() {
    // We don't want to start callhome on dev environments
    if (this.environment.isDev()) {
      log.info("Skip callhome scheduling");
      return;
    }
    log.info("Initialize callhome service");
    platformScheduler.schedule(
        getClass().getSimpleName(),
        Duration.ZERO,
        Duration.ofMinutes(YB_CALLHOME_INTERVAL),
        this::scheduleRunner);
  }

  @VisibleForTesting
  void scheduleRunner() {
    try {
      log.info("Running scheduler");
      for (Customer c : Customer.getAll()) {
        try {
          callHomeManager.sendDiagnostics(c);
        } catch (Exception e) {
          log.error("Error sending callhome for customer: " + c.getUuid(), e);
        }
      }
    } catch (Exception e) {
      log.error("Error sending callhome", e);
    }
  }
}
