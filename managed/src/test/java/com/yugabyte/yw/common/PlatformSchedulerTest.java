// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import com.yugabyte.yw.models.HighAvailabilityConfig;
import java.time.Duration;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import org.apache.pekko.actor.Cancellable;
import org.junit.After;
import org.junit.Test;

/**
 * Covers the switchover gate. A restore drops and recreates every table under whatever queries are
 * running, so no scheduled work may be in flight while one is happening.
 */
public class PlatformSchedulerTest extends FakeDBApplication {

  private Cancellable cancellable;

  @After
  public void tearDown() {
    HighAvailabilityConfig.setSwitchOverInProgress(false);
    if (cancellable != null) {
      cancellable.cancel();
    }
  }

  @Test
  public void testNothingRunsWhileASwitchOverIsInProgress() throws Exception {
    PlatformScheduler scheduler = app.injector().instanceOf(PlatformScheduler.class);
    AtomicInteger runs = new AtomicInteger();
    HighAvailabilityConfig.setSwitchOverInProgress(true);

    // scheduleAlwaysOn, because that is the one the follower-side schedules use and the one the
    // follower check does not already cover.
    cancellable =
        scheduler.scheduleAlwaysOn(
            "test-schedule", Duration.ZERO, Duration.ofMillis(100), runs::incrementAndGet);

    Thread.sleep(1000);
    assertEquals(0, runs.get());
  }

  @Test
  public void testScheduleRunsOnceTheSwitchOverIsOver() throws Exception {
    PlatformScheduler scheduler = app.injector().instanceOf(PlatformScheduler.class);
    CountDownLatch ran = new CountDownLatch(1);
    cancellable =
        scheduler.scheduleAlwaysOn(
            "test-schedule", Duration.ZERO, Duration.ofMillis(100), ran::countDown);

    assertTrue(ran.await(30, TimeUnit.SECONDS));
  }
}
