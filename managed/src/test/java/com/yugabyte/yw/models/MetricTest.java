// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.models;

import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.equalTo;
import static org.hamcrest.Matchers.notNullValue;
import static org.hamcrest.Matchers.nullValue;
import static org.junit.Assert.assertTrue;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.alerts.MetricService;
import com.yugabyte.yw.models.filters.MetricFilter;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import com.yugabyte.yw.models.helpers.PlatformMetrics;
import java.time.Instant;
import java.time.temporal.ChronoUnit;
import java.util.Date;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class MetricTest extends FakeDBApplication {

  private final Instant testStart = Instant.now();

  private Customer customer;

  private Universe universe;

  @InjectMocks private MetricService metricService;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer("Customer");
    universe = ModelFactory.createUniverse();
  }

  @Test
  public void testAddAndGetByKey() {
    metricService.setOkStatusMetric(
        metricService.buildMetricTemplate(PlatformMetrics.ALERT_MANAGER_STATUS, universe));

    MetricKey key =
        MetricKey.builder()
            .customerUuid(customer.getUuid())
            .name(PlatformMetrics.ALERT_MANAGER_STATUS.getMetricName())
            .targetUuid(universe.getUniverseUUID())
            .build();
    Metric metric = metricService.get(key);

    assertMetric(metric, 1.0);
  }

  @Test
  public void testUpdateAndGetByKey() {
    metricService.setStatusMetric(
        metricService.buildMetricTemplate(PlatformMetrics.ALERT_MANAGER_STATUS, universe), "Error");

    metricService.setOkStatusMetric(
        metricService.buildMetricTemplate(PlatformMetrics.ALERT_MANAGER_STATUS, universe));

    MetricKey key =
        MetricKey.builder()
            .customerUuid(customer.getUuid())
            .name(PlatformMetrics.ALERT_MANAGER_STATUS.getMetricName())
            .targetUuid(universe.getUniverseUUID())
            .build();
    Metric metric = metricService.get(key);

    assertMetric(metric, 1.0);
  }

  @Test
  public void testDelete() {
    metricService.setStatusMetric(
        metricService.buildMetricTemplate(PlatformMetrics.ALERT_MANAGER_STATUS, universe), "Error");

    MetricKey key =
        MetricKey.builder()
            .customerUuid(customer.getUuid())
            .name(PlatformMetrics.ALERT_MANAGER_STATUS.getMetricName())
            .targetUuid(universe.getUniverseUUID())
            .build();

    MetricFilter metricFilter = MetricFilter.builder().key(key).build();

    metricService.delete(metricFilter);

    Metric metric = metricService.get(key);
    assertThat(metric, nullValue());
  }

  private void assertMetric(Metric metric, double value) {
    assertThat(metric.getUuid(), notNullValue());
    assertThat(metric.getCreateTime(), notNullValue());
    assertThat(metric.getUpdateTime(), notNullValue());
    assertTrue(
        metric
            .getExpireTime()
            .after(
                Date.from(
                    testStart.plus(MetricService.DEFAULT_METRIC_EXPIRY_SEC, ChronoUnit.SECONDS))));
    assertTrue(
        metric
            .getExpireTime()
            .before(
                Date.from(
                    testStart.plus(
                        MetricService.DEFAULT_METRIC_EXPIRY_SEC + 10, ChronoUnit.SECONDS))));
    assertThat(metric.getCustomerUUID(), equalTo(customer.getUuid()));
    assertThat(metric.getType(), equalTo(Metric.Type.GAUGE));
    assertThat(metric.getName(), equalTo(PlatformMetrics.ALERT_MANAGER_STATUS.getMetricName()));
    assertThat(metric.getValue(), equalTo(value));
    assertThat(metric.getLabelValue(KnownAlertLabels.TARGET_TYPE), equalTo("universe"));
    assertThat(metric.getLabelValue(KnownAlertLabels.TARGET_NAME), equalTo(universe.name));
    assertThat(
        metric.getLabelValue(KnownAlertLabels.TARGET_UUID),
        equalTo(universe.getUniverseUUID().toString()));
    assertThat(metric.getLabelValue(KnownAlertLabels.UNIVERSE_NAME), equalTo(universe.name));
    assertThat(
        metric.getLabelValue(KnownAlertLabels.UNIVERSE_UUID),
        equalTo(universe.getUniverseUUID().toString()));
    assertThat(metric.getLabelValue(KnownAlertLabels.ERROR_MESSAGE), nullValue());
  }
}
