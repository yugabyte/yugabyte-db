// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.logging;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import java.time.Duration;
import lombok.extern.slf4j.Slf4j;
import org.apache.pekko.actor.Cancellable;

/**
 * Expires log archives named before the rollover pattern gained %i, which logback never deletes.
 * Runs daily, on HA standbys too, and stops once none are left.
 */
@Slf4j
@Singleton
public class LegacyLogArchiveCleanup {

  private static final Duration INTERVAL = Duration.ofDays(1);

  private final PlatformScheduler platformScheduler;
  private final SettableRuntimeConfigFactory sConfigFactory;
  private final Config config;
  private volatile Cancellable schedule;

  @Inject
  public LegacyLogArchiveCleanup(
      PlatformScheduler platformScheduler,
      SettableRuntimeConfigFactory sConfigFactory,
      Config config) {
    this.platformScheduler = platformScheduler;
    this.sConfigFactory = sConfigFactory;
    this.config = config;
  }

  public void start() {
    if (LogUtil.expireLegacyLogArchives(sConfigFactory, config) == 0) {
      return;
    }
    schedule =
        platformScheduler.scheduleAlwaysOn(
            getClass().getSimpleName(), INTERVAL, INTERVAL, this::scheduleRunner);
  }

  void scheduleRunner() {
    if (LogUtil.expireLegacyLogArchives(sConfigFactory, config) == 0 && schedule != null) {
      schedule.cancel();
      log.info("No legacy log archives left, stopped {}", getClass().getSimpleName());
    }
  }
}
