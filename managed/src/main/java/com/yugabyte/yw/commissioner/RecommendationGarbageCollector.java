// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import com.google.common.annotations.VisibleForTesting;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.common.config.CustomerConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.Customer;
import io.prometheus.client.CollectorRegistry;
import io.prometheus.client.Counter;
import java.time.Duration;
import java.time.Instant;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import org.yb.perf_advisor.filters.PerformanceRecommendationFilter;
import org.yb.perf_advisor.services.db.PerformanceRecommendationService;

@Singleton
@Slf4j
public class RecommendationGarbageCollector {

  // Counter names
  static final String RECOMMENDATION_METRIC_NAME = "ybp_recommendation_gc_count";
  static final String NUM_REC_GC_RUNS = "ybp_recommendation_gc_run_count";
  static final String NUM_REC_GC_ERRORS = "ybp_recommendation_gc_error_count";

  // Counter label
  static final String CUSTOMER_UUID_LABEL = "customer_uuid";

  // Counters
  private static Counter PURGED_PERF_RECOMMENDATION_COUNT;
  private static Counter NUM_RECOMMENDATION_GC_RUNS_COUNT;
  private static Counter NUM_RECOMMENDATION_GC_ERRORS_COUNT;

  static {
    registerMetrics();
  }

  private final PlatformScheduler platformScheduler;
  private final RuntimeConfGetter confGetter;
  private final PerformanceRecommendationService performanceRecommendationService;

  @Inject
  public RecommendationGarbageCollector(
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
    NUM_RECOMMENDATION_GC_RUNS_COUNT =
        Counter.build(
                NUM_REC_GC_RUNS, "Number of times performance_recommendation gc checks are run")
            .register(CollectorRegistry.defaultRegistry);
    NUM_RECOMMENDATION_GC_ERRORS_COUNT =
        Counter.build(
                NUM_REC_GC_ERRORS, "Number of failed performance_recommendation delete attempts")
            .register(CollectorRegistry.defaultRegistry);
  }

  public void start() {
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
    try {
      Customer.getAll().forEach(this::checkCustomerAndPurgeRecs);
    } catch (Exception e) {
      log.error("Error running PA recommendation garbage collector", e);
    }
  }

  @VisibleForTesting
  void checkCustomerAndPurgeRecs(Customer c) {
    Instant createdInstantTimestamp = Instant.now().minus(perfRecommendationRetentionDuration(c));
    PerformanceRecommendationFilter filter =
        PerformanceRecommendationFilter.builder()
            .createdInstantBefore(createdInstantTimestamp)
            .isStale(true)
            .build();
    NUM_RECOMMENDATION_GC_RUNS_COUNT.inc();
    try {
      int numRowsGCdInThisRun = this.performanceRecommendationService.delete(filter);
      PURGED_PERF_RECOMMENDATION_COUNT.labels(c.getUuid().toString()).inc(numRowsGCdInThisRun);
      log.info("Garbage collected {} rows", numRowsGCdInThisRun);
    } catch (Exception e) {
      log.error("Error deleting rows: {}", e);
      NUM_RECOMMENDATION_GC_ERRORS_COUNT.inc();
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
}
