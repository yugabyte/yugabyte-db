package com.yugabyte.yw.commissioner;

import static com.yugabyte.yw.commissioner.PerfAdvisorGarbageCollector.CUSTOMER_UUID_LABEL;
import static com.yugabyte.yw.commissioner.PerfAdvisorGarbageCollector.NUM_REC_GC_ERRORS;
import static com.yugabyte.yw.commissioner.PerfAdvisorGarbageCollector.NUM_REC_GC_RUNS;
import static com.yugabyte.yw.commissioner.PerfAdvisorGarbageCollector.PA_RUN_METRIC_NAME;
import static com.yugabyte.yw.commissioner.PerfAdvisorGarbageCollector.RECOMMENDATION_METRIC_NAME;
import static io.prometheus.client.CollectorRegistry.defaultRegistry;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.notNullValue;
import static org.hamcrest.Matchers.nullValue;
import static org.junit.Assert.assertEquals;
import static org.mockito.Mockito.spy;

import com.yugabyte.yw.common.FakePerfAdvisorDBTest;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.UniversePerfAdvisorRun;
import java.time.Duration;
import java.time.Instant;
import java.time.temporal.ChronoUnit;
import java.util.Date;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mockito;
import org.mockito.junit.MockitoJUnitRunner;
import org.yb.perf_advisor.models.PerformanceRecommendation;

@RunWith(MockitoJUnitRunner.class)
public class PerfAdvisorGarbageCollectorTest extends FakePerfAdvisorDBTest {

  private void checkCounters(
      Customer customer,
      Double expectedNumRuns,
      Double expectedErrors,
      Double expectedPerfRecommendationGC,
      Double expectedPerfAdvisorRunGC) {
    assertEquals(
        expectedNumRuns, defaultRegistry.getSampleValue(getTotalCounterName(NUM_REC_GC_RUNS)));
    assertEquals(
        expectedErrors, defaultRegistry.getSampleValue(getTotalCounterName(NUM_REC_GC_ERRORS)));
    assertEquals(
        expectedPerfRecommendationGC,
        defaultRegistry.getSampleValue(
            getTotalCounterName(RECOMMENDATION_METRIC_NAME),
            new String[] {CUSTOMER_UUID_LABEL},
            new String[] {customer.getUuid().toString()}));
    assertEquals(
        expectedPerfAdvisorRunGC,
        defaultRegistry.getSampleValue(
            getTotalCounterName(PA_RUN_METRIC_NAME),
            new String[] {CUSTOMER_UUID_LABEL},
            new String[] {customer.getUuid().toString()}));
  }

  private PerfAdvisorGarbageCollector recommendationGarbageCollector;

  @Before
  public void setUp() {
    defaultRegistry.clear();
    PerfAdvisorGarbageCollector.registerMetrics();
    recommendationGarbageCollector =
        spy(app.injector().instanceOf(PerfAdvisorGarbageCollector.class));
  }

  @Test
  public void testStart_disabled() throws InterruptedException {
    PerformanceRecommendation toClean = createRecommendationToClean(true);
    Mockito.doReturn(Duration.ZERO).when(recommendationGarbageCollector).gcCheckInterval();
    recommendationGarbageCollector.start();
    Thread.sleep(200);

    toClean = performanceRecommendationService.get(toClean.getId());
    assertThat(toClean, notNullValue());
  }

  @Test
  public void testStart_enabled() throws InterruptedException {
    PerformanceRecommendation toClean = createRecommendationToClean(true);
    Mockito.doReturn(Duration.ofMillis(30)).when(recommendationGarbageCollector).gcCheckInterval();
    recommendationGarbageCollector.start();
    for (int i = 0; i < 100; i++) {
      Double recommendationsGCed =
          defaultRegistry.getSampleValue(
              getTotalCounterName(RECOMMENDATION_METRIC_NAME),
              new String[] {CUSTOMER_UUID_LABEL},
              new String[] {customer.getUuid().toString()});
      if (recommendationsGCed != null && recommendationsGCed > 0) {
        // Wait while PA GC run happens for up to 10 seconds.
        break;
      }
      Thread.sleep(100);
    }
    toClean = performanceRecommendationService.get(toClean.getId());
    assertThat(toClean, nullValue());
  }

  @Test
  public void testPurge_noneStale() throws InterruptedException {
    PerformanceRecommendation toClean = createRecommendationToClean(false);

    recommendationGarbageCollector.checkCustomerAndPurgeRecs(customer);

    toClean = performanceRecommendationService.get(toClean.getId());
    assertThat(toClean, notNullValue());
    checkCounters(customer, 1.0, 0.0, 0.0, 0.0);
  }

  @Test
  public void testPurge() {
    PerformanceRecommendation toClean = createRecommendationToClean(true);

    UniversePerfAdvisorRun run =
        UniversePerfAdvisorRun.create(customer.getUuid(), universe.getUniverseUUID(), true);
    run.setScheduleTime(Date.from(Instant.now().minus(31, ChronoUnit.DAYS)));
    run.save();

    recommendationGarbageCollector.checkCustomerAndPurgeRecs(customer);

    toClean = performanceRecommendationService.get(toClean.getId());
    assertThat(toClean, nullValue());
    checkCounters(customer, 1.0, 0.0, 1.0, 1.0);
  }

  // Test that if delete fails we increment error counter
  @Test
  public void testPurge_invalidData() {
    RuntimeConfGetter confGetter = app.injector().instanceOf(RuntimeConfGetter.class);
    recommendationGarbageCollector = new PerfAdvisorGarbageCollector(null, confGetter, null);

    recommendationGarbageCollector.checkCustomerAndPurgeRecs(customer);

    checkCounters(customer, 1.0, 1.0, null, null);
  }

  private String getTotalCounterName(String name) {
    return name + "_total";
  }

  private PerformanceRecommendation createRecommendationToClean(boolean isStale) {
    PerformanceRecommendation toClean = createTestRecommendation();
    toClean = performanceRecommendationService.save(toClean);

    toClean.setIsStale(isStale);
    toClean.setRecommendationTimestamp(Instant.now().minus(31, ChronoUnit.DAYS));
    perfAdvisorEbeanServer.save(toClean);
    return toClean;
  }
}
