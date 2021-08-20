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

import static com.yugabyte.yw.models.Metric.createQueryByFilter;
import static com.yugabyte.yw.models.helpers.CommonUtils.nowPlusWithoutMillis;
import static com.yugabyte.yw.models.helpers.EntityOperation.CREATE;
import static com.yugabyte.yw.models.helpers.EntityOperation.UPDATE;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.common.YWServiceException;
import com.yugabyte.yw.common.concurrent.MultiKeyLock;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Metric;
import com.yugabyte.yw.models.MetricKey;
import com.yugabyte.yw.models.MetricLabel;
import com.yugabyte.yw.models.MetricTargetKey;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.filters.MetricFilter;
import com.yugabyte.yw.models.filters.MetricFilter.MetricFilterBuilder;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.EntityOperation;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import com.yugabyte.yw.models.helpers.PlatformMetrics;
import io.ebean.annotation.Transactional;
import java.time.temporal.ChronoUnit;
import java.util.Collection;
import java.util.Collections;
import java.util.Comparator;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.TimeUnit;
import java.util.function.Function;
import java.util.stream.Collectors;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections.CollectionUtils;
import org.apache.commons.lang3.StringUtils;

@Singleton
@Slf4j
public class MetricService {
  public static final long DEFAULT_METRIC_EXPIRY_SEC = TimeUnit.DAYS.toSeconds(10);
  public static final double STATUS_OK = 1D;
  public static final double STATUS_NOT_OK = 0D;
  private static final Comparator<MetricTargetKey> METRIC_TARGET_KEY_COMPARATOR =
      Comparator.comparing(MetricTargetKey::getName)
          .thenComparing(MetricTargetKey::getCustomerUuid)
          .thenComparing(MetricTargetKey::getTargetUuid);
  private static final Comparator<MetricKey> METRIC_KEY_COMPARATOR =
      Comparator.comparing(MetricKey::getTargetKey, METRIC_TARGET_KEY_COMPARATOR)
          .thenComparing(MetricKey::getTargetLabels);
  private final MultiKeyLock<MetricKey> metricKeyLock = new MultiKeyLock<>(METRIC_KEY_COMPARATOR);

  @Transactional
  public List<Metric> save(List<Metric> metrics) {
    if (CollectionUtils.isEmpty(metrics)) {
      return metrics;
    }

    try {
      acquireLocks(metrics);
      List<Metric> beforeMetrics = Collections.emptyList();
      Set<MetricKey> metricKeys = metrics.stream().map(MetricKey::from).collect(Collectors.toSet());
      if (!metricKeys.isEmpty()) {
        MetricFilter filter = MetricFilter.builder().keys(metricKeys).build();
        beforeMetrics = list(filter);
      }
      Map<MetricKey, Metric> beforeMetricMap =
          beforeMetrics.stream().collect(Collectors.toMap(MetricKey::from, Function.identity()));

      Map<EntityOperation, List<Metric>> toCreateAndUpdate =
          metrics
              .stream()
              .peek(this::validate)
              .map(metric -> prepareForSave(metric, beforeMetricMap.get(MetricKey.from(metric))))
              .collect(Collectors.groupingBy(metric -> metric.isNew() ? CREATE : UPDATE));

      if (toCreateAndUpdate.containsKey(CREATE)) {
        List<Metric> toCreate = toCreateAndUpdate.get(CREATE);
        toCreate.forEach(Metric::generateUUID);
        Metric.db().saveAll(toCreate);
      }

      if (toCreateAndUpdate.containsKey(UPDATE)) {
        List<Metric> toUpdate = toCreateAndUpdate.get(UPDATE);
        Metric.db().updateAll(toUpdate);
      }

      log.trace("{} metrics saved", metrics.size());
      return metrics;
    } finally {
      releaseLocks(metrics);
    }
  }

  @Transactional
  public Metric save(Metric metric) {
    return save(Collections.singletonList(metric)).get(0);
  }

  public Metric get(MetricKey key) {
    MetricFilter filter = MetricFilter.builder().key(key).build();
    return createQueryByFilter(filter).findOneOrEmpty().orElse(null);
  }

  public List<Metric> list(MetricFilter filter) {
    return createQueryByFilter(filter).findList();
  }

  @Transactional
  public void delete(MetricFilter filter) {
    List<Metric> toDelete = list(filter);
    if (toDelete.isEmpty()) {
      return;
    }
    try {
      acquireLocks(toDelete);
      MetricFilter deleteFilter =
          MetricFilter.builder()
              .uuids(toDelete.stream().map(Metric::getUuid).collect(Collectors.toSet()))
              .build();
      int deleted = createQueryByFilter(deleteFilter).delete();
      log.trace("{} metrics deleted", deleted);
    } finally {
      releaseLocks(toDelete);
    }
  }

  @Transactional
  public void cleanAndSave(List<Metric> toSave, List<MetricFilter> toClean) {
    toClean.forEach(this::delete);
    save(toSave);
  }

  @Transactional
  public void cleanAndSave(List<Metric> toSave, MetricFilter toClean) {
    delete(toClean);
    save(toSave);
  }

  @Transactional
  public void cleanAndSave(List<Metric> toSave) {
    if (CollectionUtils.isEmpty(toSave)) {
      return;
    }
    MetricFilter toClean =
        MetricFilter.builder()
            .keys(toSave.stream().map(MetricKey::from).collect(Collectors.toSet()))
            .build();
    cleanAndSave(toSave, toClean);
  }

  @Transactional
  public void setOkStatusMetric(Metric metric) {
    setStatusMetric(metric, StringUtils.EMPTY);
  }

  @Transactional
  public void setFailureStatusMetric(Metric metric) {
    metric.setValue(STATUS_NOT_OK);
    cleanAndSave(Collections.singletonList(metric));
  }

  @Transactional
  public void setStatusMetric(Metric metric, String message) {
    boolean isSuccess = StringUtils.isEmpty(message);
    metric.setValue(isSuccess ? STATUS_OK : STATUS_NOT_OK);
    if (!isSuccess) {
      metric.setLabel(KnownAlertLabels.ERROR_MESSAGE, message);
    }
    cleanAndSave(Collections.singletonList(metric));
  }

  @Transactional
  public void setMetric(Metric metric, double value) {
    metric.setValue(value);
    cleanAndSave(Collections.singletonList(metric));
  }

  @Transactional
  public void handleTargetRemoval(UUID customerUuid, UUID targetUuid) {
    MetricFilterBuilder filter = MetricFilter.builder().customerUuid(customerUuid);
    if (targetUuid != null) {
      filter.targetUuid(targetUuid);
    }
    delete(filter.build());
  }

  private void validate(Metric metric) {
    if (metric.getType() == null) {
      throw new YWServiceException(BAD_REQUEST, "Type field is mandatory");
    }
    if (StringUtils.isEmpty(metric.getName())) {
      throw new YWServiceException(BAD_REQUEST, "Name field is mandatory");
    }
    if (metric.getValue() == null) {
      throw new YWServiceException(BAD_REQUEST, "Value field is mandatory");
    }
  }

  private Metric prepareForSave(Metric metric, Metric before) {
    List<MetricLabel> newTargetLabels =
        metric.getLabels().stream().filter(MetricLabel::isTargetLabel).collect(Collectors.toList());
    String newTargetLabelStr = Metric.getTargetLabelsStr(newTargetLabels);
    Metric result = before == null ? metric : before;
    result.setTargetLabels(newTargetLabelStr);
    result.setUpdateTime(CommonUtils.nowWithoutMillis());
    if (before != null) {
      result.setValue(metric.getValue());
      result.setLabels(metric.getLabels());
      result.setExpireTime(metric.getExpireTime());
    }
    return result;
  }

  private void acquireLocks(Collection<Metric> metrics) {
    Set<MetricKey> keys = metrics.stream().map(MetricKey::from).collect(Collectors.toSet());
    metricKeyLock.acquireLocks(keys);
  }

  private void releaseLocks(Collection<Metric> metrics) {
    Set<MetricKey> keys = metrics.stream().map(MetricKey::from).collect(Collectors.toSet());
    metricKeyLock.releaseLocks(keys);
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
        .setTargetUuid(customer.getUuid())
        .setLabels(MetricLabelsBuilder.create().appendTarget(customer).getMetricLabels());
  }

  public static Metric buildMetricTemplate(PlatformMetrics metric, Universe universe) {
    return buildMetricTemplate(metric, universe, DEFAULT_METRIC_EXPIRY_SEC);
  }

  public static Metric buildMetricTemplate(
      PlatformMetrics metric, Universe universe, long metricExpiryPeriodSec) {
    Customer customer = Customer.get(universe.customerId);
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
        .setTargetUuid(universe.getUniverseUUID())
        .setLabels(MetricLabelsBuilder.create().appendTarget(universe).getMetricLabels());
  }
}
