// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import static com.yugabyte.yw.common.PlatformScheduler.schedulerMetric;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import com.yugabyte.yw.common.metrics.MetricService;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.Metric;
import com.yugabyte.yw.models.MetricKey;
import com.yugabyte.yw.models.filters.MetricFilter;
import com.yugabyte.yw.models.helpers.PlatformMetrics;
import java.time.Duration;
import java.time.Instant;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import org.apache.pekko.actor.Cancellable;
import org.junit.After;
import org.junit.Test;

/**
 * Covers the switchover gate and the liveness metrics. A restore drops and recreates every table
 * under whatever queries are running, so no scheduled work may be in flight while one is happening.
 */
public class PlatformSchedulerTest extends FakeDBApplication {

  private Cancellable cancellable;
  private final List<Cancellable> others = new ArrayList<>();
  private final CountDownLatch release = new CountDownLatch(1);

  @After
  public void tearDown() {
    HighAvailabilityConfig.setSwitchOverInProgress(false);
    release.countDown();
    if (cancellable != null) {
      cancellable.cancel();
    }
    others.forEach(Cancellable::cancel);
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

  @Test
  public void testRegistrationExportsIntervalAndFirstDueTime() {
    PlatformScheduler scheduler = app.injector().instanceOf(PlatformScheduler.class);
    long before = Instant.now().getEpochSecond();
    cancellable =
        scheduler.schedule("test-schedule", Duration.ofHours(1), Duration.ofMinutes(10), () -> {});

    assertEquals(600D, metric(PlatformMetrics.SCHEDULER_INTERVAL, "test-schedule"), 0);
    double lastOk = metric(PlatformMetrics.SCHEDULER_LAST_OK_TIME, "test-schedule");
    assertTrue(lastOk >= before + 3600 && lastOk <= Instant.now().getEpochSecond() + 3600);
  }

  @Test
  public void testCompletedRunUpdatesLastOkTime() throws Exception {
    PlatformScheduler scheduler = app.injector().instanceOf(PlatformScheduler.class);
    AtomicInteger runs = new AtomicInteger();
    CountDownLatch secondRun = new CountDownLatch(1);
    cancellable =
        scheduler.schedule(
            "test-schedule",
            Duration.ZERO,
            Duration.ofMillis(100),
            () -> {
              if (runs.incrementAndGet() == 1) {
                clearLastOkTime("test-schedule");
              } else {
                secondRun.countDown();
              }
            });

    assertTrue(secondRun.await(30, TimeUnit.SECONDS));
    assertTrue(metric(PlatformMetrics.SCHEDULER_LAST_OK_TIME, "test-schedule") > 0);
  }

  @Test
  public void testHungRunDoesNotUpdateLastOkTime() throws Exception {
    PlatformScheduler scheduler = app.injector().instanceOf(PlatformScheduler.class);
    CountDownLatch started = new CountDownLatch(1);
    cancellable =
        scheduler.schedule(
            "test-schedule",
            Duration.ZERO,
            Duration.ofMillis(100),
            () -> {
              clearLastOkTime("test-schedule");
              started.countDown();
              awaitRelease();
            });

    assertTrue(started.await(30, TimeUnit.SECONDS));
    // With a fixed delay, no later tick arrives while the run hangs.
    Thread.sleep(1000);
    assertEquals(0D, metric(PlatformMetrics.SCHEDULER_LAST_OK_TIME, "test-schedule"), 0);
  }

  @Test
  public void testFailedRunDoesNotUpdateLastOkTime() throws Exception {
    PlatformScheduler scheduler = app.injector().instanceOf(PlatformScheduler.class);
    AtomicInteger runs = new AtomicInteger();
    cancellable =
        scheduler.schedule(
            "test-schedule",
            Duration.ZERO,
            Duration.ofMillis(100),
            () -> {
              clearLastOkTime("test-schedule");
              runs.incrementAndGet();
              throw new RuntimeException("test failure");
            });

    Thread.sleep(1000);
    assertTrue(runs.get() > 1);
    assertEquals(0D, metric(PlatformMetrics.SCHEDULER_LAST_OK_TIME, "test-schedule"), 0);
  }

  @Test
  public void testCancelRemovesMetricsAndFreesTheName() {
    PlatformScheduler scheduler = app.injector().instanceOf(PlatformScheduler.class);
    Cancellable first =
        scheduler.schedule("test-schedule", Duration.ofHours(1), Duration.ofMinutes(1), () -> {});
    cancellable =
        scheduler.schedule("test-schedule", Duration.ofHours(1), Duration.ofMinutes(2), () -> {});
    assertEquals(60D, metric(PlatformMetrics.SCHEDULER_INTERVAL, "test-schedule"), 0);
    assertEquals(120D, metric(PlatformMetrics.SCHEDULER_INTERVAL, "test-schedule-2"), 0);

    first.cancel();
    assertNull(metricOrNull(PlatformMetrics.SCHEDULER_INTERVAL, "test-schedule"));
    assertNull(metricOrNull(PlatformMetrics.SCHEDULER_LAST_OK_TIME, "test-schedule"));

    others.add(
        scheduler.schedule("test-schedule", Duration.ofHours(1), Duration.ofMinutes(3), () -> {}));
    assertEquals(180D, metric(PlatformMetrics.SCHEDULER_INTERVAL, "test-schedule"), 0);
  }

  @Test
  public void testSecondCancelKeepsTheMetricsOfALaterScheduleWithTheName() {
    PlatformScheduler scheduler = app.injector().instanceOf(PlatformScheduler.class);
    Cancellable first =
        scheduler.schedule("test-schedule", Duration.ofHours(1), Duration.ofMinutes(1), () -> {});
    first.cancel();
    cancellable =
        scheduler.schedule("test-schedule", Duration.ofHours(1), Duration.ofMinutes(2), () -> {});

    first.cancel();

    assertEquals(120D, metric(PlatformMetrics.SCHEDULER_INTERVAL, "test-schedule"), 0);
    others.add(
        scheduler.schedule("test-schedule", Duration.ofHours(1), Duration.ofMinutes(3), () -> {}));
    assertEquals(180D, metric(PlatformMetrics.SCHEDULER_INTERVAL, "test-schedule-2"), 0);
  }

  @Test
  public void testRunThatCancelsItsScheduleLeavesNoMetrics() throws Exception {
    PlatformScheduler scheduler = app.injector().instanceOf(PlatformScheduler.class);
    CountDownLatch ran = new CountDownLatch(1);
    Cancellable[] self = new Cancellable[1];
    CountDownLatch registered = new CountDownLatch(1);
    self[0] =
        scheduler.schedule(
            "test-schedule",
            Duration.ofMillis(100),
            Duration.ofHours(1),
            () -> {
              try {
                registered.await();
              } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
              }
              self[0].cancel();
              ran.countDown();
            });
    registered.countDown();

    assertTrue(ran.await(30, TimeUnit.SECONDS));
    // The run returns after cancelling; give its metric write time to happen.
    Thread.sleep(500);
    assertNull(metricOrNull(PlatformMetrics.SCHEDULER_LAST_OK_TIME, "test-schedule"));
    assertNull(metricOrNull(PlatformMetrics.SCHEDULER_INTERVAL, "test-schedule"));
  }

  @Test
  public void testRunRefreshesTheInterval() throws Exception {
    PlatformScheduler scheduler = app.injector().instanceOf(PlatformScheduler.class);
    MetricService metricService = app.injector().instanceOf(MetricService.class);
    CountDownLatch secondRun = new CountDownLatch(2);
    cancellable =
        scheduler.schedule(
            "test-schedule",
            Duration.ZERO,
            Duration.ofMillis(100),
            () -> {
              if (secondRun.getCount() == 2) {
                // Stands in for the interval series expiring.
                metricService.delete(
                    MetricFilter.builder()
                        .keys(
                            List.of(
                                MetricKey.from(
                                    schedulerMetric(
                                        PlatformMetrics.SCHEDULER_INTERVAL, "test-schedule"))))
                        .build());
              }
              secondRun.countDown();
            });

    assertTrue(secondRun.await(30, TimeUnit.SECONDS));
    Thread.sleep(200);
    assertEquals(0.1D, metric(PlatformMetrics.SCHEDULER_INTERVAL, "test-schedule"), 0);
  }

  @Test
  public void testOneOffScheduleExportsNothing() {
    PlatformScheduler scheduler = app.injector().instanceOf(PlatformScheduler.class);
    cancellable = scheduler.scheduleOnce("test-schedule", Duration.ofHours(1), () -> {});
    assertNull(metricOrNull(PlatformMetrics.SCHEDULER_INTERVAL, "test-schedule"));
  }

  private void awaitRelease() {
    try {
      release.await();
    } catch (InterruptedException e) {
      Thread.currentThread().interrupt();
    }
  }

  private void clearLastOkTime(String name) {
    app.injector()
        .instanceOf(MetricService.class)
        .setMetric(schedulerMetric(PlatformMetrics.SCHEDULER_LAST_OK_TIME, name), 0);
  }

  private double metric(PlatformMetrics metric, String name) {
    Metric result = metricOrNull(metric, name);
    assertTrue(name + " has no " + metric, result != null);
    return result.getValue();
  }

  private Metric metricOrNull(PlatformMetrics metric, String name) {
    return app.injector()
        .instanceOf(MetricService.class)
        .get(MetricKey.from(schedulerMetric(metric, name)));
  }
}
