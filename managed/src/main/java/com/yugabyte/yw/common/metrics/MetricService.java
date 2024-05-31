/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.common.metrics;

import static com.yugabyte.yw.models.helpers.CommonUtils.nowPlusWithoutMillis;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Metric;
import com.yugabyte.yw.models.MetricKey;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.filters.MetricFilter;
import com.yugabyte.yw.models.filters.MetricFilter.MetricFilterBuilder;
import com.yugabyte.yw.models.helpers.MetricSourceState;
import com.yugabyte.yw.models.helpers.PlatformMetrics;
import java.time.temporal.ChronoUnit;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.TimeUnit;
import java.util.stream.Collectors;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

@Singleton
@Slf4j
public class MetricService {
  public static final long DEFAULT_METRIC_EXPIRY_SEC = TimeUnit.DAYS.toSeconds(10);
  public static final double STATUS_OK = 1D;
  public static final double STATUS_NOT_OK = 0D;

  private final MetricStorage metricStorage;

  @Inject
  public MetricService(MetricStorage metricStorage) {
    this.metricStorage = metricStorage;
  }

  public void save(List<Metric> metrics) {
    metricStorage.save(metrics);
  }

  public void save(Metric metric) {
    save(Collections.singletonList(metric));
  }

  public void cleanAndSave(List<Metric> toSave, MetricFilter toClean) {
    Set<MetricKey> toSaveKeys = toSave.stream().map(MetricKey::from).collect(Collectors.toSet());
    metricStorage.delete(toClean.toBuilder().keysExcluded(toSaveKeys).build());
    metricStorage.save(toSave);
  }

  public void delete(MetricFilter metricFilter) {
    metricStorage.delete(metricFilter);
  }

  public Metric get(MetricKey key) {
    return metricStorage.get(key);
  }

  public List<Metric> list(MetricFilter filter) {
    List<Metric> result = new ArrayList<>();
    metricStorage.process(filter, result::add);
    return result;
  }

  public void setOkStatusMetric(Metric metric) {
    setMetric(metric, STATUS_OK);
  }

  public void setFailureStatusMetric(Metric metric) {
    setMetric(metric, STATUS_NOT_OK);
  }

  public void setMetric(Metric metric, double value) {
    metric.setValue(value);
    save(Collections.singletonList(metric));
  }

  public void markSourceActive(UUID customerUuid, UUID sourceUuid) {
    metricStorage.markSource(customerUuid, sourceUuid, MetricSourceState.ACTIVE);
  }

  public void markSourceInactive(UUID customerUuid, UUID sourceUuid) {
    MetricFilterBuilder filter =
        MetricFilter.builder()
            .customerUuid(customerUuid)
            .metricNames(PlatformMetrics.invalidForState(MetricSourceState.INACTIVE));
    if (sourceUuid != null) {
      filter.sourceUuid(sourceUuid);
    }
    metricStorage.markSource(customerUuid, sourceUuid, MetricSourceState.INACTIVE);
    metricStorage.delete(filter.build());
  }

  public void markSourceRemoved(UUID customerUuid, UUID sourceUuid) {
    MetricFilterBuilder filter = MetricFilter.builder().customerUuid(customerUuid);
    if (sourceUuid != null) {
      filter.sourceUuid(sourceUuid);
    }
    metricStorage.markSource(customerUuid, sourceUuid, MetricSourceState.REMOVED);
    metricStorage.delete(filter.build());
  }

  private void validate(Metric metric) {
    if (metric.getType() == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Type field is mandatory");
    }
    if (StringUtils.isEmpty(metric.getName())) {
      throw new PlatformServiceException(BAD_REQUEST, "Name field is mandatory");
    }
    if (metric.getValue() == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Value field is mandatory");
    }
  }

  public static Metric buildMetricTemplate(PlatformMetrics metric) {
    return buildMetricTemplate(metric, DEFAULT_METRIC_EXPIRY_SEC);
  }

  public static Metric buildMetricTemplate(PlatformMetrics metric, long metricExpiryPeriodSec) {
    return new Metric()
        .setExpireTime(nowPlusWithoutMillis(metricExpiryPeriodSec, ChronoUnit.SECONDS))
        .setType(Metric.Type.GAUGE)
        .setName(metric.getMetricName());
  }

  public static Metric buildMetricTemplate(PlatformMetrics metric, Customer customer) {
    return buildMetricTemplate(metric, customer, DEFAULT_METRIC_EXPIRY_SEC);
  }

  public static Metric buildMetricTemplate(
      PlatformMetrics metric, Customer customer, long metricExpiryPeriodSec) {
    return buildMetricTemplate(metric, metricExpiryPeriodSec)
        .setCustomerUUID(customer.getUuid())
        .setSourceUuid(customer.getUuid())
        .setLabels(MetricLabelsBuilder.create().appendSource(customer).getMetricLabels());
  }

  public static Metric buildMetricTemplate(PlatformMetrics metric, Universe universe) {
    return buildMetricTemplate(metric, universe, DEFAULT_METRIC_EXPIRY_SEC);
  }

  public static Metric buildMetricTemplate(
      PlatformMetrics metric, Universe universe, long metricExpiryPeriodSec) {
    Customer customer = Customer.get(universe.getCustomerId());
    return buildMetricTemplate(metric, customer, universe, metricExpiryPeriodSec);
  }

  public static Metric buildMetricTemplate(
      PlatformMetrics metric, Customer customer, Universe universe) {
    return buildMetricTemplate(metric, customer, universe, DEFAULT_METRIC_EXPIRY_SEC);
  }

  public static Metric buildMetricTemplate(
      PlatformMetrics metric, Customer customer, Universe universe, long metricExpiryPeriodSec) {
    return buildMetricTemplate(metric, metricExpiryPeriodSec)
        .setCustomerUUID(customer.getUuid())
        .setSourceUuid(universe.getUniverseUUID())
        .setLabels(MetricLabelsBuilder.create().appendSource(universe).getMetricLabels());
  }
}
