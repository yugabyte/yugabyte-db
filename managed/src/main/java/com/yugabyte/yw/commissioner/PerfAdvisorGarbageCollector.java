// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import static com.yugabyte.yw.commissioner.PerfAdvisorScheduler.DATABASE_DRIVER_PARAM;

import com.google.common.annotations.VisibleForTesting;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.common.config.CustomerConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.UniversePerfAdvisorRun;
import io.prometheus.client.CollectorRegistry;
import io.prometheus.client.Counter;
import java.time.Duration;
import java.time.Instant;
import java.util.Date;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import org.yb.perf_advisor.filters.PerformanceRecommendationFilter;
import org.yb.perf_advisor.services.db.PerformanceRecommendationService;

@Singleton
@Slf4j
public class PerfAdvisorGarbageCollector {

  // Counter names
  static final String RECOMMENDATION_METRIC_NAME = "ybp_recommendation_gc_count";

  static final String PA_RUN_METRIC_NAME = "ybp_perf_advisor_run_gc_count";
  static final String NUM_REC_GC_RUNS = "ybp_pa_gc_run_count";
  static final String NUM_REC_GC_ERRORS = "ybp_pa_gc_error_count";

  // Counter label
  static final String CUSTOMER_UUID_LABEL = "customer_uuid";

  // Counters
  private static Counter PURGED_PERF_RECOMMENDATION_COUNT;
  private static Counter PURGED_PERF_ADVISOR_RUNS_COUNT;
  private static Counter NUM_PA_GC_RUNS_COUNT;
  private static Counter NUM_PA_GC_ERRORS_COUNT;

  static {
    registerMetrics();
  }

  private final PlatformScheduler platformScheduler;
  private final RuntimeConfGetter confGetter;
  private final PerformanceRecommendationService performanceRecommendationService;

  @Inject
  public PerfAdvisorGarbageCollector(
      PlatformScheduler platformScheduler,
      RuntimeConfGetter confGetter,
      PerformanceRecommendationService perfService) {
    this.platformScheduler = platformScheduler;
    this.confGetter = confGetter;
    this.performanceRecommendationService = perfService;
  }

  @VisibleForTesting
  static void registerMetrics() {
    PURGED_PERF_RECOMMENDATION_COUNT =
        Counter.build(
                RECOMMENDATION_METRIC_NAME,
                "Number of old completed perf-advisor recommendations purged for a customer")
            .labelNames(CUSTOMER_UUID_LABEL)
            .register(CollectorRegistry.defaultRegistry);
    PURGED_PERF_ADVISOR_RUNS_COUNT =
        Counter.build(PA_RUN_METRIC_NAME, "Number of old perf advisor runs purged for a customer")
            .labelNames(CUSTOMER_UUID_LABEL)
            .register(CollectorRegistry.defaultRegistry);
    NUM_PA_GC_RUNS_COUNT =
        Counter.build(NUM_REC_GC_RUNS, "Number of times perf advisor gc checks are run")
            .register(CollectorRegistry.defaultRegistry);
    NUM_PA_GC_ERRORS_COUNT =
        Counter.build(NUM_REC_GC_ERRORS, "Number of failed perf advisor gc attempts")
            .register(CollectorRegistry.defaultRegistry);
  }

  public void start() {
    if (!confGetter
        .getStaticConf()
        .getString(DATABASE_DRIVER_PARAM)
        .equals("org.postgresql.Driver")) {
      log.debug(
          "Skipping perf advisor GC initialization for tests"
              + " or other non-postgresql environment");
      return;
    }
    Duration gcInterval = this.gcCheckInterval();
    if (gcInterval.isZero()) {
      log.info("yb.perf_advisor.cleanup.gc_check_interval set to 0.");
      log.warn("!!! PA Recommendation GC DISABLED !!!");
    } else {
      log.info("Scheduling PerfAdvisor recommendation GC every " + gcInterval);
      platformScheduler.schedule(
          getClass().getSimpleName(),
          Duration.ZERO, // InitialDelay
          gcInterval,
          this::scheduleRunner);
    }
  }

  private void scheduleRunner() {
    if (HighAvailabilityConfig.isFollower()) {
      log.debug("Skipping perf advisor GC run for follower platform");
      return;
    }
    try {
      Customer.getAll().forEach(this::checkCustomerAndPurgeRecs);
    } catch (Exception e) {
      log.error("Error running PA recommendation garbage collection", e);
    }
  }

  @VisibleForTesting
  void checkCustomerAndPurgeRecs(Customer c) {
    NUM_PA_GC_RUNS_COUNT.inc();
    try {
      Instant createdInstantTimestamp = Instant.now().minus(perfRecommendationRetentionDuration(c));
      PerformanceRecommendationFilter filter =
          PerformanceRecommendationFilter.builder()
              .createdInstantBefore(createdInstantTimestamp)
              .isStale(true)
              .build();

      int numPerfRecommendationsDeleted = this.performanceRecommendationService.delete(filter);
      PURGED_PERF_RECOMMENDATION_COUNT
          .labels(c.getUuid().toString())
          .inc(numPerfRecommendationsDeleted);
      log.info("Garbage collected {} performance recommendations", numPerfRecommendationsDeleted);

      Instant scheduledInstantTimestamp = Instant.now().minus(perfAdvisorRunRetentionDuration(c));
      int numPaRunsDeleted =
          UniversePerfAdvisorRun.deleteOldRuns(c.getUuid(), Date.from(scheduledInstantTimestamp));
      PURGED_PERF_ADVISOR_RUNS_COUNT.labels(c.getUuid().toString()).inc(numPaRunsDeleted);
      log.info("Garbage collected {} performance advisor runs", numPaRunsDeleted);
    } catch (Exception e) {
      log.error("Error deleting rows", e);
      NUM_PA_GC_ERRORS_COUNT.inc();
    }
  }

  /** The interval at which the gc checker will run. */
  @VisibleForTesting
  Duration gcCheckInterval() {
    return confGetter.getStaticConf().getDuration("yb.perf_advisor.cleanup.gc_check_interval");
  }

  /**
   * For how many days to retain a perf-advisor performance recommendation entry before garbage
   * collecting it.
   */
  private Duration perfRecommendationRetentionDuration(Customer customer) {
    return confGetter.getConfForScope(
        customer, CustomerConfKeys.perfRecommendationRetentionDuration);
  }

  private Duration perfAdvisorRunRetentionDuration(Customer customer) {
    return confGetter.getConfForScope(customer, CustomerConfKeys.perfAdvisorRunRetentionDuration);
  }
}
