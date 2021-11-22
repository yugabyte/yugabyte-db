// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.common.metrics;

import static com.yugabyte.yw.common.metrics.MetricService.buildMetricTemplate;
import static com.yugabyte.yw.models.helpers.CommonUtils.datePlus;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.containsInAnyOrder;
import static org.hamcrest.Matchers.empty;
import static org.hamcrest.Matchers.equalTo;
import static org.hamcrest.Matchers.hasSize;
import static org.hamcrest.Matchers.not;
import static org.hamcrest.Matchers.notNullValue;
import static org.hamcrest.Matchers.nullValue;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;

import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Metric;
import com.yugabyte.yw.models.MetricKey;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.filters.MetricFilter;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import com.yugabyte.yw.models.helpers.PlatformMetrics;
import java.time.Instant;
import java.time.temporal.ChronoUnit;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Date;
import java.util.List;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import lombok.extern.slf4j.Slf4j;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
@Slf4j
public class MetricServiceTest extends FakeDBApplication {

  private final Instant testStart = Instant.now();

  private Customer customer;

  private Universe universe;

  private MetricService metricService;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer("Customer");
    universe = ModelFactory.createUniverse();
    metricService = app.injector().instanceOf(MetricService.class);
  }

  @Test
  public void testAddAndGetByKey() {
    metricService.setOkStatusMetric(
        buildMetricTemplate(PlatformMetrics.ALERT_MANAGER_STATUS, universe));

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
        buildMetricTemplate(PlatformMetrics.ALERT_MANAGER_STATUS, universe)
            .setKeyLabel(KnownAlertLabels.NODE_NAME, "node1"),
        "Error");

    metricService.setOkStatusMetric(
        buildMetricTemplate(PlatformMetrics.ALERT_MANAGER_STATUS, universe)
            .setKeyLabel(KnownAlertLabels.NODE_NAME, "node1"));

    MetricKey key =
        MetricKey.builder()
            .customerUuid(customer.getUuid())
            .name(PlatformMetrics.ALERT_MANAGER_STATUS.getMetricName())
            .targetUuid(universe.getUniverseUUID())
            .sourceLabels("node_name:node1")
            .build();
    Metric metric = metricService.get(key);

    assertMetric(metric, 1.0);
  }

  @Test
  public void testUpdateFromMultipleThreads() throws InterruptedException {
    ExecutorService executor = Executors.newFixedThreadPool(2);
    List<Future<Void>> futures = new ArrayList<>();
    for (int i = 0; i < 2; i++) {
      int value = i;
      futures.add(
          executor.submit(
              () -> {
                metricService.setMetric(
                    buildMetricTemplate(PlatformMetrics.UNIVERSE_INACTIVE_CRON_NODES, universe),
                    value);
                return null;
              }));
    }
    for (Future<Void> future : futures) {
      try {
        future.get();
      } catch (ExecutionException e) {
        fail("Exception occurred in worker: " + e);
      }
    }
    executor.shutdown();
  }

  @Test
  public void testConcurrentMetricUpdateAndCustomerDelete() throws InterruptedException {
    ExecutorService executor = Executors.newFixedThreadPool(2);
    List<Metric> metrics =
        ImmutableList.of(
            buildMetricTemplate(PlatformMetrics.UNIVERSE_EXISTS, universe).setValue(1D),
            buildMetricTemplate(PlatformMetrics.UNIVERSE_PAUSED, universe).setValue(1D),
            buildMetricTemplate(PlatformMetrics.UNIVERSE_UPDATE_IN_PROGRESS, universe)
                .setValue(1D));
    metricService.save(metrics);
    metricService.flushMetricsToDb();

    List<Metric> updatedMetrics =
        ImmutableList.of(
            buildMetricTemplate(PlatformMetrics.UNIVERSE_EXISTS, universe)
                .setValue(0D)
                .setLabel(KnownAlertLabels.NODE_NAME, "qwerty"),
            buildMetricTemplate(PlatformMetrics.UNIVERSE_PAUSED, universe)
                .setValue(0D)
                .setLabel(KnownAlertLabels.NODE_NAME, "qwerty1"),
            buildMetricTemplate(PlatformMetrics.UNIVERSE_UPDATE_IN_PROGRESS, universe)
                .setValue(0D)
                .setLabels(Collections.emptyList()));
    metricService.save(updatedMetrics);

    Future<?> metricFlushFuture = executor.submit(() -> metricService.flushMetricsToDb());
    Future<?> customerRemovalFuture =
        executor.submit(
            () -> {
              customer.delete();
              metricService.handleSourceRemoval(customer.getUuid(), null);
            });
    try {
      customerRemovalFuture.get();
    } catch (ExecutionException e) {
      log.error("Exception occurred in customer removal worker", e);
      fail("Exception occurred in customer removal worker: " + e);
    }
    try {
      metricFlushFuture.get();
    } catch (ExecutionException e) {
      log.info("Exception occurred in metric flush worker. Will retry:", e);
      metricService.flushMetricsToDb();
      log.info("Metrics saved successfully on retry.");
    }
    executor.shutdown();
  }

  @Test
  public void testMetricStorageInitialization() {
    List<Metric> metrics =
        ImmutableList.of(
            buildMetricTemplate(PlatformMetrics.UNIVERSE_EXISTS, universe).setValue(1D),
            buildMetricTemplate(PlatformMetrics.UNIVERSE_PAUSED, universe).setValue(1D),
            buildMetricTemplate(PlatformMetrics.UNIVERSE_UPDATE_IN_PROGRESS, universe)
                .setValue(1D));
    metricService.save(metrics);

    List<Metric> inMemoryMetrics = metricService.list(MetricFilter.builder().build());
    assertThat(inMemoryMetrics, containsInAnyOrder(metrics.toArray(new Metric[0])));
    List<Metric> persistedMetrics = metricService.list(MetricFilter.builder().build(), true);
    assertThat(persistedMetrics, empty());

    metricService.flushMetricsToDb();

    inMemoryMetrics = metricService.list(MetricFilter.builder().build());
    assertThat(inMemoryMetrics, containsInAnyOrder(metrics.toArray(new Metric[0])));
    persistedMetrics = metricService.list(MetricFilter.builder().build(), true);
    assertThat(persistedMetrics, containsInAnyOrder(metrics.toArray(new Metric[0])));

    MetricStorage newStorage = new MetricStorage();
    MetricService newService = new MetricService(newStorage);

    inMemoryMetrics = newService.list(MetricFilter.builder().build());
    assertThat(inMemoryMetrics, empty());

    newService.initialize();

    inMemoryMetrics = newService.list(MetricFilter.builder().build());
    assertThat(inMemoryMetrics, hasSize(3));
    assertThat(inMemoryMetrics, containsInAnyOrder(metrics.toArray(new Metric[0])));
  }

  @Test
  public void testDelete() {
    metricService.setStatusMetric(
        buildMetricTemplate(PlatformMetrics.ALERT_MANAGER_STATUS, universe), "Error");
    metricService.setOkStatusMetric(
        buildMetricTemplate(PlatformMetrics.HEALTH_CHECK_STATUS, universe));
    metricService.flushMetricsToDb();

    MetricKey keyToDelete =
        MetricKey.builder()
            .customerUuid(customer.getUuid())
            .name(PlatformMetrics.ALERT_MANAGER_STATUS.getMetricName())
            .targetUuid(universe.getUniverseUUID())
            .build();
    MetricKey keyRemaining =
        MetricKey.builder()
            .customerUuid(customer.getUuid())
            .name(PlatformMetrics.HEALTH_CHECK_STATUS.getMetricName())
            .targetUuid(universe.getUniverseUUID())
            .build();

    MetricFilter filterToDelete = MetricFilter.builder().key(keyToDelete).build();
    MetricFilter filterRemaining = MetricFilter.builder().key(keyRemaining).build();

    metricService.delete(filterToDelete);

    List<Metric> deletedMetric = metricService.list(filterToDelete);
    List<Metric> remainingMetric = metricService.list(filterRemaining);

    assertThat(deletedMetric, empty());
    assertThat(remainingMetric, hasSize(1));

    List<Metric> deletedMetricInDb = metricService.list(filterToDelete, true);
    List<Metric> remainingMetricInDb = metricService.list(filterRemaining, true);

    // Both metrics are still in DB.
    assertThat(deletedMetricInDb, hasSize(1));
    assertThat(remainingMetricInDb, hasSize(1));

    metricService.flushMetricsToDb();

    deletedMetricInDb = metricService.list(filterToDelete, true);
    remainingMetricInDb = metricService.list(filterRemaining, true);

    // Only remaining metric left in DB.
    assertThat(deletedMetricInDb, empty());
    assertThat(remainingMetricInDb, hasSize(1));
  }

  @Test
  public void testSaveAndClean() {
    Metric node1Metric =
        buildMetricTemplate(PlatformMetrics.UNIVERSE_NODE_FUNCTION, universe)
            .setKeyLabel(KnownAlertLabels.NODE_NAME, "node1")
            .setValue(1D);
    Metric node2Metric =
        buildMetricTemplate(PlatformMetrics.UNIVERSE_NODE_FUNCTION, universe)
            .setKeyLabel(KnownAlertLabels.NODE_NAME, "node2")
            .setValue(2D);
    Metric node3Metric =
        buildMetricTemplate(PlatformMetrics.UNIVERSE_NODE_FUNCTION, universe)
            .setKeyLabel(KnownAlertLabels.NODE_NAME, "node3")
            .setValue(3D);
    Metric node4Metric =
        buildMetricTemplate(PlatformMetrics.UNIVERSE_NODE_FUNCTION, universe)
            .setKeyLabel(KnownAlertLabels.NODE_NAME, "node4")
            .setValue(4D);
    List<Metric> metrics = ImmutableList.of(node1Metric, node2Metric, node3Metric, node4Metric);
    metricService.save(metrics);

    List<Metric> newMetrics =
        ImmutableList.of(
            buildMetricTemplate(PlatformMetrics.UNIVERSE_NODE_FUNCTION, universe)
                .setKeyLabel(KnownAlertLabels.NODE_NAME, "node1")
                .setExpireTime(datePlus(node1Metric.getExpireTime(), 12, ChronoUnit.HOURS))
                .setValue(1D),
            buildMetricTemplate(PlatformMetrics.UNIVERSE_NODE_FUNCTION, universe)
                .setExpireTime(datePlus(node2Metric.getExpireTime(), 12, ChronoUnit.HOURS))
                .setKeyLabel(KnownAlertLabels.NODE_NAME, "node2")
                .setValue(3D),
            buildMetricTemplate(PlatformMetrics.UNIVERSE_NODE_FUNCTION, universe)
                .setKeyLabel(KnownAlertLabels.NODE_NAME, "node3")
                .setExpireTime(datePlus(node3Metric.getExpireTime(), 36, ChronoUnit.HOURS))
                .setValue(3D));
    MetricFilter toClean =
        MetricFilter.builder().metricName(PlatformMetrics.UNIVERSE_NODE_FUNCTION).build();

    metricService.cleanAndSave(newMetrics, toClean);

    Metric updatedNode1Metric = metricService.get(MetricKey.from(node1Metric));
    Metric updatedNode2Metric = metricService.get(MetricKey.from(node2Metric));
    Metric updatedNode3Metric = metricService.get(MetricKey.from(node3Metric));
    Metric updatedNode4Metric = metricService.get(MetricKey.from(node4Metric));

    assertThat(updatedNode1Metric.getExpireTime(), equalTo(node1Metric.getExpireTime()));
    assertThat(updatedNode2Metric.getValue(), equalTo(3D));
    assertThat(updatedNode2Metric.getExpireTime(), not(equalTo(node1Metric.getExpireTime())));
    assertThat(updatedNode3Metric.getValue(), equalTo(3D));
    assertThat(updatedNode3Metric.getExpireTime(), not(equalTo(node1Metric.getExpireTime())));
    assertThat(updatedNode4Metric, nullValue());
  }

  private void assertMetric(Metric metric, double value) {
    assertThat(metric.getCreateTime(), notNullValue());
    assertThat(metric.getUpdateTime(), notNullValue());
    assertFalse(
        metric
            .getExpireTime()
            .before(
                Date.from(
                    testStart.plus(
                        MetricService.DEFAULT_METRIC_EXPIRY_SEC - 1, ChronoUnit.SECONDS))));
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
    assertThat(metric.getLabelValue(KnownAlertLabels.SOURCE_TYPE), equalTo("universe"));
    assertThat(metric.getLabelValue(KnownAlertLabels.SOURCE_NAME), equalTo(universe.name));
    assertThat(
        metric.getLabelValue(KnownAlertLabels.SOURCE_UUID),
        equalTo(universe.getUniverseUUID().toString()));
    assertThat(metric.getLabelValue(KnownAlertLabels.UNIVERSE_NAME), equalTo(universe.name));
    assertThat(
        metric.getLabelValue(KnownAlertLabels.UNIVERSE_UUID),
        equalTo(universe.getUniverseUUID().toString()));
    assertThat(metric.getLabelValue(KnownAlertLabels.ERROR_MESSAGE), nullValue());
  }
}
