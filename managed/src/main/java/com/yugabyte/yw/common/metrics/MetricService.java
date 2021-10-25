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
import static com.yugabyte.yw.models.helpers.CommonUtils.getDurationSeconds;
import static com.yugabyte.yw.models.helpers.CommonUtils.nowPlusWithoutMillis;
import static com.yugabyte.yw.models.helpers.EntityOperation.CREATE;
import static com.yugabyte.yw.models.helpers.EntityOperation.UPDATE;
import static play.mvc.Http.Status.BAD_REQUEST;

import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.concurrent.MultiKeyLock;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Metric;
import com.yugabyte.yw.models.MetricKey;
import com.yugabyte.yw.models.MetricLabel;
import com.yugabyte.yw.models.MetricSourceKey;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.filters.MetricFilter;
import com.yugabyte.yw.models.filters.MetricFilter.MetricFilterBuilder;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.EntityOperation;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import com.yugabyte.yw.models.helpers.PlatformMetrics;
import io.ebean.annotation.Transactional;
import io.prometheus.client.CollectorRegistry;
import io.prometheus.client.Counter;
import java.time.temporal.ChronoUnit;
import java.util.Collection;
import java.util.Collections;
import java.util.Comparator;
import java.util.Date;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.TimeUnit;
import java.util.function.Function;
import java.util.stream.Collectors;
import java.util.stream.Stream;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections.CollectionUtils;
import org.apache.commons.lang3.StringUtils;

@Singleton
@Slf4j
public class MetricService {
  public static final long DEFAULT_METRIC_EXPIRY_SEC = TimeUnit.DAYS.toSeconds(10);
  public static final double EXPIRY_TIME_UPDATE_COEFFICIENT = 1.1;
  public static final double STATUS_OK = 1D;
  public static final double STATUS_NOT_OK = 0D;

  private static final Comparator<MetricSourceKey> METRIC_SOURCE_KEY_COMPARATOR =
      Comparator.comparing(MetricSourceKey::getName)
          .thenComparing(MetricSourceKey::getCustomerUuid)
          .thenComparing(MetricSourceKey::getSourceUuid);
  private static final Comparator<MetricKey> METRIC_KEY_COMPARATOR =
      Comparator.comparing(MetricKey::getSourceKey, METRIC_SOURCE_KEY_COMPARATOR)
          .thenComparing(
              MetricKey::getSourceLabels, Comparator.nullsFirst(Comparator.naturalOrder()));
  private final MultiKeyLock<MetricKey> metricKeyLock = new MultiKeyLock<>(METRIC_KEY_COMPARATOR);

  // Counter names
  private static final String METRIC_UPDATE_COUNT = "ybp_metric_update_count";
  private static final String METRIC_DELETE_COUNT = "ybp_metric_delete_count";

  // Counters
  private static final Counter METRIC_UPDATE_COUNTER =
      Counter.build(METRIC_UPDATE_COUNT, "Number of created/updated metric values")
          .labelNames("persistent")
          .register(CollectorRegistry.defaultRegistry);
  private static final Counter METRIC_DELETE_COUNTER =
      Counter.build(METRIC_DELETE_COUNT, "Number of deleted metric values")
          .labelNames("persistent")
          .register(CollectorRegistry.defaultRegistry);

  private final MetricStorage metricStorage;

  @Inject
  public MetricService(MetricStorage metricStorage) {
    this.metricStorage = metricStorage;
  }

  public List<Metric> save(List<Metric> metrics) {
    try {
      acquireLocks(metrics);
      return saveInternal(metrics, false);
    } finally {
      releaseLocks(metrics);
    }
  }

  private List<Metric> saveInternal(List<Metric> metrics, boolean persist) {
    List<Metric> beforeMetrics = Collections.emptyList();
    Set<MetricKey> metricKeys = metrics.stream().map(MetricKey::from).collect(Collectors.toSet());
    if (!metricKeys.isEmpty()) {
      MetricFilter filter = MetricFilter.builder().keys(metricKeys).build();
      beforeMetrics = list(filter, persist);
    }
    Map<MetricKey, Metric> beforeMetricMap =
        beforeMetrics.stream().collect(Collectors.toMap(MetricKey::from, Function.identity()));

    Map<EntityOperation, List<Metric>> toCreateAndUpdate =
        metrics
            .stream()
            .peek(this::validate)
            .filter(metric -> filterForSave(metric, beforeMetricMap.get(MetricKey.from(metric))))
            .map(metric -> prepareForSave(metric, beforeMetricMap.get(MetricKey.from(metric))))
            .collect(Collectors.groupingBy(metric -> metric.isNew() ? CREATE : UPDATE));

    List<Metric> toCreate = toCreateAndUpdate.getOrDefault(CREATE, Collections.emptyList());
    if (!toCreate.isEmpty()) {
      log.trace("Creating metrics {} in {} storage", toCreate, persist ? "DB" : "Memory");
      if (persist) {
        toCreate.forEach(Metric::generateUUID);
        Metric.db().saveAll(toCreate);
      } else {
        metricStorage.save(toCreate);
      }
    }

    List<Metric> toUpdate = toCreateAndUpdate.getOrDefault(UPDATE, Collections.emptyList());
    if (!toUpdate.isEmpty()) {
      log.trace("Updating metrics {} in {} storage", toUpdate, persist ? "DB" : "Memory");
      if (persist) {
        Metric.db().updateAll(toUpdate);
      } else {
        metricStorage.save(toUpdate);
      }
    }

    int savedMetrics = toCreate.size() + toUpdate.size();
    METRIC_UPDATE_COUNTER.labels(String.valueOf(persist)).inc(savedMetrics);
    log.trace("{} metrics saved", savedMetrics);
    return metrics;
  }

  public Metric save(Metric metric) {
    return save(Collections.singletonList(metric)).get(0);
  }

  public Metric get(MetricKey key) {
    return metricStorage.get(key);
  }

  public List<Metric> list(MetricFilter filter) {
    return list(filter, false);
  }

  public List<Metric> list(MetricFilter filter, boolean persisted) {
    if (persisted) {
      return createQueryByFilter(filter).findList();
    } else {
      return metricStorage.list(filter);
    }
  }

  public void delete(MetricFilter filter) {
    List<Metric> toDelete = list(filter);
    if (toDelete.isEmpty()) {
      return;
    }
    try {
      acquireLocks(toDelete);
      deleteInternal(toDelete, false);
    } finally {
      releaseLocks(toDelete);
    }
  }

  private void deleteInternal(Collection<Metric> toDelete, boolean persist) {
    if (toDelete.isEmpty()) {
      return;
    }
    int deleted;
    log.trace("Deleting metrics {} from {} storage", toDelete, persist ? "DB" : "Memory");
    if (persist) {
      MetricFilter deleteFilter =
          MetricFilter.builder()
              .keys(toDelete.stream().map(MetricKey::from).collect(Collectors.toSet()))
              .build();
      deleted = createQueryByFilter(deleteFilter).delete();
    } else {
      deleted = metricStorage.delete(toDelete);
    }

    METRIC_DELETE_COUNTER.labels(String.valueOf(persist)).inc(deleted);
    log.trace("{} metrics deleted", deleted);
  }

  public void cleanAndSave(List<Metric> toSave, List<MetricFilter> toClean) {
    Map<MetricKey, Metric> toDelete =
        toClean
            .stream()
            .map(this::list)
            .flatMap(Collection::stream)
            .collect(Collectors.toMap(MetricKey::from, Function.identity(), (a, b) -> a));
    Set<MetricKey> toSaveKeys = toSave.stream().map(MetricKey::from).collect(Collectors.toSet());
    toSaveKeys.forEach(toDelete::remove);
    Set<MetricKey> allKeys =
        Stream.concat(toSaveKeys.stream(), toDelete.keySet().stream()).collect(Collectors.toSet());

    try {
      metricKeyLock.acquireLocks(allKeys);
      if (!toSave.isEmpty()) {
        saveInternal(toSave, false);
      }
      if (!toDelete.isEmpty()) {
        deleteInternal(toDelete.values(), false);
      }
    } finally {
      metricKeyLock.releaseLocks(allKeys);
    }
  }

  public void cleanAndSave(List<Metric> toSave, MetricFilter toClean) {
    cleanAndSave(toSave, ImmutableList.of(toClean));
  }

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

  public void setOkStatusMetric(Metric metric) {
    setStatusMetric(metric, StringUtils.EMPTY);
  }

  public void setFailureStatusMetric(Metric metric) {
    metric.setValue(STATUS_NOT_OK);
    cleanAndSave(Collections.singletonList(metric));
  }

  public void setStatusMetric(Metric metric, String message) {
    boolean isSuccess = StringUtils.isEmpty(message);
    metric.setValue(isSuccess ? STATUS_OK : STATUS_NOT_OK);
    if (!isSuccess) {
      metric.setLabel(KnownAlertLabels.ERROR_MESSAGE, message);
    }
    cleanAndSave(Collections.singletonList(metric));
  }

  public void setMetric(Metric metric, double value) {
    metric.setValue(value);
    cleanAndSave(Collections.singletonList(metric));
  }

  public void handleSourceRemoval(UUID customerUuid, UUID sourceUuid) {
    MetricFilterBuilder filter = MetricFilter.builder().customerUuid(customerUuid);
    if (sourceUuid != null) {
      filter.sourceUuid(sourceUuid);
    }
    delete(filter.build());
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

  private boolean filterForSave(Metric metric, Metric before) {
    if (before == null) {
      return true;
    }
    if (!Objects.equals(before.getValue(), metric.getValue())) {
      return true;
    }
    Date now = new Date();
    double oldExpiryDuration = getDurationSeconds(now, before.getExpireTime());
    long newExpiryDuration = getDurationSeconds(now, metric.getExpireTime());
    // Update if expiry time becomes less OR if expiry time grows at least for 10%
    // - once a day for default expiry period.
    if (newExpiryDuration < oldExpiryDuration
        || oldExpiryDuration * EXPIRY_TIME_UPDATE_COEFFICIENT < newExpiryDuration) {
      return true;
    }
    return false;
  }

  private Metric prepareForSave(Metric metric, Metric before) {
    List<MetricLabel> newSourceLabels =
        metric.getLabels().stream().filter(MetricLabel::isSourceLabel).collect(Collectors.toList());
    String newSourceLabelStr = Metric.getSourceLabelsStr(newSourceLabels);
    Metric result = before == null ? metric : before;
    result.setSourceLabels(newSourceLabelStr);
    result.setUpdateTime(CommonUtils.nowWithoutMillis());
    if (before != null) {
      result.setValue(metric.getValue());
      result.setLabels(metric.getLabels());
      result.setExpireTime(metric.getExpireTime());
    } else if (result.getUuid() != null) {
      log.warn("Trying to save metric with uuid, which is not found in storage: {}", result);
    }
    return result;
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
        .setSourceUuid(universe.getUniverseUUID())
        .setLabels(MetricLabelsBuilder.create().appendSource(universe).getMetricLabels());
  }

  private void acquireLocks(Collection<Metric> metrics) {
    Set<MetricKey> keys = metrics.stream().map(MetricKey::from).collect(Collectors.toSet());
    metricKeyLock.acquireLocks(keys);
  }

  private void releaseLocks(Collection<Metric> metrics) {
    Set<MetricKey> keys = metrics.stream().map(MetricKey::from).collect(Collectors.toSet());
    metricKeyLock.releaseLocks(keys);
  }

  @Transactional
  public void flushMetricsToDb() {
    Set<MetricKey> dirtyKeys = metricStorage.getDirtyMetrics();
    Set<MetricKey> deletedKeys = metricStorage.getDeletedMetrics();
    Set<MetricKey> keysToLock =
        Stream.concat(dirtyKeys.stream(), deletedKeys.stream()).collect(Collectors.toSet());
    if (CollectionUtils.isEmpty(keysToLock)) {
      return;
    }
    try {
      Set<UUID> customerUuids =
          keysToLock
              .stream()
              .map(MetricKey::getSourceKey)
              .map(MetricSourceKey::getCustomerUuid)
              .filter(Objects::nonNull)
              .collect(Collectors.toSet());
      // This is required to avoid DB deadlock on customer delete +
      // parallel metric save/delete operation
      if (CollectionUtils.isNotEmpty(customerUuids)) {
        Customer.getForUpdate(customerUuids);
      }
      metricKeyLock.acquireLocks(keysToLock);
      if (CollectionUtils.isNotEmpty(dirtyKeys)) {
        // querying in-memory metrics here to update persistent storage.
        List<Metric> toSave = list(MetricFilter.builder().keys(dirtyKeys).build(), false);
        saveInternal(toSave, true);
        metricStorage.getDirtyMetrics().removeAll(dirtyKeys);
      }
      if (CollectionUtils.isNotEmpty(deletedKeys)) {
        // querying persistent metrics here to delete them.
        List<Metric> toDelete = list(MetricFilter.builder().keys(deletedKeys).build(), true);
        deleteInternal(toDelete, true);
        metricStorage.getDeletedMetrics().removeAll(deletedKeys);
      }
    } finally {
      metricKeyLock.releaseLocks(keysToLock);
    }
  }

  void initialize() {
    metricStorage.initialize(list(MetricFilter.builder().build(), true));
  }
}
