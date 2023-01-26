package com.yugabyte.yw.commissioner;

import static com.yugabyte.yw.commissioner.RecommendationGarbageCollector.RECOMMENDATION_METRIC_NAME;
import static com.yugabyte.yw.commissioner.RecommendationGarbageCollector.CUSTOMER_UUID_LABEL;
import static com.yugabyte.yw.commissioner.RecommendationGarbageCollector.NUM_REC_GC_ERRORS;
import static com.yugabyte.yw.commissioner.RecommendationGarbageCollector.NUM_REC_GC_RUNS;
import static io.prometheus.client.CollectorRegistry.defaultRegistry;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.eq;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.verifyZeroInteractions;
import static org.mockito.Mockito.when;

import com.typesafe.config.Config;
import com.yugabyte.yw.common.PlatformScheduler;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.models.Customer;
import java.time.Duration;
import java.util.UUID;
import junit.framework.TestCase;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;
import org.yb.perf_advisor.filters.PerformanceRecommendationFilter;
import org.yb.perf_advisor.services.db.PerformanceRecommendationService;

@RunWith(MockitoJUnitRunner.class)
public class RecommendationGarbageCollectorTest extends TestCase {

  private void checkCounters(
      UUID customerUuid,
      Double expectedNumRuns,
      Double expectedErrors,
      Double expectedPerfRecommendationGC) {
    assertEquals(
        expectedNumRuns, defaultRegistry.getSampleValue(getTotalCounterName(NUM_REC_GC_RUNS)));
    assertEquals(
        expectedErrors, defaultRegistry.getSampleValue(getTotalCounterName(NUM_REC_GC_ERRORS)));
    assertEquals(
        expectedPerfRecommendationGC,
        defaultRegistry.getSampleValue(
            getTotalCounterName(RECOMMENDATION_METRIC_NAME),
            new String[] {CUSTOMER_UUID_LABEL},
            new String[] {customerUuid.toString()}));
  }

  @Mock PlatformScheduler mockPlatformScheduler;

  @Mock PerformanceRecommendationService mockPerfRecService;

  @Mock RuntimeConfigFactory mockRuntimeConfFactory;

  @Mock Config mockAppConfig;

  @Mock Customer mockCustomer;

  @Mock RuntimeConfGetter mockConfGetter;

  private RecommendationGarbageCollector RecommendationGarbageCollector;

  @Before
  public void setUp() {
    when(mockRuntimeConfFactory.globalRuntimeConf()).thenReturn(mockAppConfig);
    RecommendationGarbageCollector =
        new RecommendationGarbageCollector(
            mockPlatformScheduler, mockConfGetter, mockPerfRecService);
    defaultRegistry.clear();
    RecommendationGarbageCollector.registerMetrics();
  }

  @Test
  public void testStart_disabled() {
    when(mockAppConfig.getDuration("yb.perf_advisor.cleanup.gc_check_interval"))
        .thenReturn(Duration.ZERO);
    RecommendationGarbageCollector.start();
    verifyZeroInteractions(mockPlatformScheduler);
  }

  @Test
  public void testStart_enabled() {
    when(mockAppConfig.getDuration("yb.perf_advisor.cleanup.gc_check_interval"))
        .thenReturn(Duration.ofDays(1));
    RecommendationGarbageCollector.start();
    verify(mockPlatformScheduler, times(1))
        .schedule(any(), eq(Duration.ZERO), eq(Duration.ofDays(1)), any());
  }

  @Test
  public void testPurge_noneStale() {
    UUID customerUuid = UUID.randomUUID();

    RecommendationGarbageCollector.checkCustomerAndPurgeRecs(mockCustomer);

    checkCounters(customerUuid, 1.0, 0.0, 0.0);
  }

  @Test
  public void testPurge() {
    UUID customerUuid = UUID.randomUUID();
    when(mockCustomer.getUuid()).thenReturn(customerUuid);
    // Pretend we deleted 5 rows in all:
    when(mockPerfRecService.delete((PerformanceRecommendationFilter) any())).thenReturn(5);

    RecommendationGarbageCollector.checkCustomerAndPurgeRecs(mockCustomer);

    checkCounters(customerUuid, 1.0, 0.0, 4.0);
  }

  // Test that if we do not delete when there are referential integrity issues; then we report such
  // error in counter.
  @Test
  public void testPurge_invalidData() {
    UUID customerUuid = UUID.randomUUID();
    // Pretend we deleted 5 rows in all:
    when(mockPerfRecService.delete((PerformanceRecommendationFilter) any())).thenReturn(0);

    RecommendationGarbageCollector.checkCustomerAndPurgeRecs(mockCustomer);

    checkCounters(customerUuid, 1.0, 1.0, 0.0);
  }

  private String getTotalCounterName(String name) {
    return name + "_total";
  }
}
