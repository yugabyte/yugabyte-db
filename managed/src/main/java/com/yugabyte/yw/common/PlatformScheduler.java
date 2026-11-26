// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import static com.yugabyte.yw.common.metrics.MetricService.buildMetricTemplate;

import com.google.common.annotations.VisibleForTesting;
import com.google.common.util.concurrent.ThreadFactoryBuilder;
import com.yugabyte.yw.common.metrics.MetricService;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.Metric;
import com.yugabyte.yw.models.MetricKey;
import com.yugabyte.yw.models.filters.MetricFilter;
import com.yugabyte.yw.models.helpers.PlatformMetrics;
import java.time.Duration;
import java.time.Instant;
import java.util.Arrays;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.function.Function;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import org.apache.pekko.actor.ActorSystem;
import org.apache.pekko.actor.Cancellable;
import scala.concurrent.ExecutionContext;

/** For easy creation of scheduler that will shutdown on app shutdown. */
@Slf4j
@Singleton
public class PlatformScheduler {
  public static final long SHUTDOWN_WAIT_TIMEOUT_MS = 60000L;
  private static final String SCHEDULER_NAME_LABEL = "scheduler_name";
  // Must outlast the "scheduler not running" alert. Cancelling the schedule deletes it.
  private static final long SCHEDULER_METRIC_EXPIRY_SEC = TimeUnit.DAYS.toSeconds(365);
  private final ActorSystem actorSystem;
  private final ExecutionContext executionContext;
  private final ShutdownHookHandler shutdownHookHandler;
  private final MetricService metricService;
  // Names of the live periodic schedules, which key their liveness metrics.
  private final Set<String> metricNames = ConcurrentHashMap.newKeySet();

  @Inject
  public PlatformScheduler(
      ActorSystem actorSystem,
      PlatformExecutorFactory executorFactory,
      ShutdownHookHandler shutdownHookHandler,
      MetricService metricService) {
    this.actorSystem = actorSystem;
    this.shutdownHookHandler = shutdownHookHandler;
    this.metricService = metricService;
    // Create a separate execution context for scheduler to use its own thread pool.
    this.executionContext =
        ExecutionContext.fromExecutor(
            executorFactory.createExecutor(
                "scheduler",
                new ThreadFactoryBuilder()
                    .setNameFormat("platform-scheduler-%d")
                    .setDaemon(true)
                    .build()));
  }

  private Cancellable createShutdownAwareSchedule(
      String name, Runnable runnable, Function<Runnable, Cancellable> scheduleFactory) {
    return createShutdownAwareSchedule(name, runnable, scheduleFactory, false, null, null);
  }

  /**
   * A periodic schedule passes its initial delay and interval, and its runs are then reported for
   * the "scheduler not running" alert. A one-off passes nulls.
   */
  private Cancellable createShutdownAwareSchedule(
      String name,
      Runnable runnable,
      Function<Runnable, Cancellable> scheduleFactory,
      boolean runOnFollower,
      Duration initialDelay,
      Duration interval) {
    final LivenessMetrics metrics =
        interval == null ? null : new LivenessMetrics(registerMetricName(name), interval);
    final AtomicBoolean isRunning = new AtomicBoolean();
    final Object lock = new Object();
    Runnable wrappedRunnable =
        () -> {
          try {
            boolean shouldRun = false;
            synchronized (lock) {
              // Synchronized block in shutdown and this should be serialized.
              // Nothing scheduled runs during a switchover, runOnFollower included: the
              // restore behind it drops and recreates every table, leaving open sessions
              // unable to run their cached plans, and queries in flight hold the locks its
              // DROP SCHEMA waits for.
              shouldRun =
                  !shutdownHookHandler.isShutdownInitiated()
                      && !HighAvailabilityConfig.isSwitchOverInProgress()
                      && (runOnFollower || !HighAvailabilityConfig.isFollower())
                      && isRunning.compareAndSet(false, true);
            }
            if (shouldRun) {
              try {
                runnable.run();
                setLastOkTime(metrics, Instant.now());
              } finally {
                isRunning.set(false);
                if (shutdownHookHandler.isShutdownInitiated()) {
                  synchronized (lock) {
                    lock.notify();
                  }
                }
              }
            } else {
              if (!isRunning.get()) {
                // Skipped as expected rather than stuck behind a run in progress.
                setLastOkTime(metrics, Instant.now());
              }
              log.warn(
                  "Previous run of scheduler {} is in progress, is being shut down, or YBA is in"
                      + " follower mode or a switchover.",
                  name);
            }
          } catch (Throwable t) {
            log.error("Scheduler run '{}' failed; it will be retried on the next tick.", name, t);
          }
        };
    if (metrics != null) {
      // The first run is due after the initial delay.
      metrics.setLastOkTime(Instant.now().plus(initialDelay));
    }
    Cancellable cancellable = scheduleFactory.apply(wrappedRunnable);
    shutdownHookHandler.addShutdownHook(
        cancellable,
        can -> {
          // Do not use the cancellable directly as it can create strong reference.
          if (can != null && !can.isCancelled()) {
            log.debug("Shutting down scheduler - {}", name);
            synchronized (lock) {
              while (isRunning.get()) {
                try {
                  lock.wait(SHUTDOWN_WAIT_TIMEOUT_MS);
                } catch (InterruptedException e) {
                  log.debug("Timed out waiting to shut down scheduler - {}", name);
                  break;
                }
              }
            }
            boolean isCancelled = can.cancel();
            log.debug("Shutdown status for scheduler - {} is {}", name, isCancelled);
          }
        });
    if (metrics == null) {
      return cancellable;
    }
    return new Cancellable() {
      @Override
      public boolean cancel() {
        boolean cancelled = cancellable.cancel();
        metrics.remove();
        return cancelled;
      }

      @Override
      public boolean isCancelled() {
        return cancellable.isCancelled();
      }
    };
  }

  // Suffixes a name another live schedule already uses, so their metrics don't mix.
  private String registerMetricName(String name) {
    String metricName = name;
    for (int i = 2; !metricNames.add(metricName); i++) {
      metricName = name + "-" + i;
    }
    if (!metricName.equals(name)) {
      log.warn("Scheduler name {} is already in use, reporting it as {}", name, metricName);
    }
    return metricName;
  }

  private static void setLastOkTime(LivenessMetrics metrics, Instant time) {
    if (metrics != null) {
      metrics.setLastOkTime(time);
    }
  }

  /** The liveness metrics of one periodic schedule. */
  private class LivenessMetrics {
    private final String name;
    private final double intervalSec;
    private boolean removed;

    LivenessMetrics(String name, Duration interval) {
      this.name = name;
      this.intervalSec = interval.toMillis() / 1000.0;
    }

    // Writes the interval too, so it expires no earlier than the last OK time. A run that cancels
    // its own schedule must not bring the metrics back.
    synchronized void setLastOkTime(Instant time) {
      if (removed) {
        return;
      }
      metricService.setMetric(
          schedulerMetric(PlatformMetrics.SCHEDULER_LAST_OK_TIME, name), time.getEpochSecond());
      metricService.setMetric(
          schedulerMetric(PlatformMetrics.SCHEDULER_INTERVAL, name), intervalSec);
    }

    // Once only: after that, the name may belong to another schedule.
    synchronized void remove() {
      if (removed) {
        return;
      }
      removed = true;
      metricService.delete(
          MetricFilter.builder()
              .keys(
                  Arrays.asList(
                      MetricKey.from(schedulerMetric(PlatformMetrics.SCHEDULER_LAST_OK_TIME, name)),
                      MetricKey.from(schedulerMetric(PlatformMetrics.SCHEDULER_INTERVAL, name))))
              .build());
      metricNames.remove(name);
    }
  }

  @VisibleForTesting
  static Metric schedulerMetric(PlatformMetrics metric, String metricName) {
    return buildMetricTemplate(metric, SCHEDULER_METRIC_EXPIRY_SEC)
        .setKeyLabel(SCHEDULER_NAME_LABEL, metricName);
  }

  public Cancellable schedule(
      String name, Duration initialDelay, Duration interval, Runnable runnable) {
    return createShutdownAwareSchedule(
        name,
        runnable,
        r ->
            actorSystem
                .scheduler()
                .scheduleWithFixedDelay(initialDelay, interval, r, executionContext),
        false,
        initialDelay,
        interval);
  }

  /**
   * Same as {@link #schedule} but the runnable is executed even when the YBA instance is an HA
   * follower. Use only for tasks that must run on standby YBAs (e.g. propagating the local embedded
   * PA configuration to the local standby PA).
   */
  public Cancellable scheduleAlwaysOn(
      String name, Duration initialDelay, Duration interval, Runnable runnable) {
    return createShutdownAwareSchedule(
        name,
        runnable,
        r ->
            actorSystem
                .scheduler()
                .scheduleWithFixedDelay(initialDelay, interval, r, executionContext),
        true,
        initialDelay,
        interval);
  }

  public Cancellable scheduleOnce(String name, Duration initialDelay, Runnable runnable) {
    return createShutdownAwareSchedule(
        name,
        runnable,
        r -> actorSystem.scheduler().scheduleOnce(initialDelay, r, executionContext));
  }
}
