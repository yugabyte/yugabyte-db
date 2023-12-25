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

import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.models.Metric;
import com.yugabyte.yw.models.MetricKey;
import com.yugabyte.yw.models.MetricSourceKey;
import com.yugabyte.yw.models.filters.MetricFilter;
import com.yugabyte.yw.models.helpers.MetricSourceState;
import com.yugabyte.yw.models.helpers.PlatformMetrics;
import java.util.ArrayList;
import java.util.Collection;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Map.Entry;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.locks.Lock;
import java.util.concurrent.locks.ReentrantLock;
import java.util.function.Consumer;
import java.util.stream.Collectors;
import java.util.stream.Stream;
import javax.inject.Singleton;
import lombok.Value;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;

/**
 * Metric store. Used to store last metric value in-memory and return the list of metrics to
 * prometheus endpoint. Used instead of regular Prometheus client classes as we need to remove
 * metrics, which are not applicable anymore - for example object is deleted. Also allows to delete
 * old expired metrics, which are not deleted explicitly.
 */
@Singleton
@Slf4j
public class MetricStorage {

  private static final UUID NULL_UUID = UUID.fromString("00000000-0000-0000-0000-000000000000");
  private final Map<String, NamedMetricStore> metricsByKey = new ConcurrentHashMap<>();
  private final Map<String, Lock> metricNameLock = new ConcurrentHashMap<>();
  private final Map<Pair<UUID, UUID>, MetricSourceState> sourceStateMap = new ConcurrentHashMap<>();

  public Metric get(MetricKey key) {
    return get(key.getSourceKey().getName())
        .flatMap(namedStore -> namedStore.get(key.getSourceKey().getCustomerUuid()))
        .flatMap(customerStore -> customerStore.get(key.getSourceKey().getSourceUuid()))
        .flatMap(sourceStore -> sourceStore.get(key.getSourceLabels()))
        .filter(metric -> !metric.isDeleted())
        .orElse(null);
  }

  public void process(MetricFilter metricFilter, Consumer<Metric> metricConsumer) {
    get(metricFilter)
        .flatMap(namedStore -> namedStore.get(metricFilter))
        .flatMap(customerStore -> customerStore.get(metricFilter))
        .flatMap(sourceStore -> sourceStore.get(metricFilter))
        .forEach(metricConsumer);
  }

  public void save(List<Metric> metrics) {
    if (CollectionUtils.isEmpty(metrics)) {
      return;
    }
    acquireLocks(metrics);
    try {
      metrics.forEach(this::save);
    } finally {
      releaseLocks(metrics);
    }
  }

  public void delete(MetricFilter filter) {
    process(filter, metric -> metric.setDeleted(true));
  }

  public void markSource(UUID customerUuid, UUID metricSource, MetricSourceState state) {
    if (customerUuid == null) {
      throw new IllegalArgumentException("Customer UUID can't be null");
    }
    log.info(
        "Setting metric source status for customer {} and source {} to {}",
        customerUuid,
        metricSource,
        state.name());
    sourceStateMap.put(new Pair<>(customerUuid, metricSource), state);
  }

  private void acquireLocks(Collection<Metric> metrics) {
    metrics.stream()
        .map(Metric::getName)
        .distinct()
        .sorted()
        .forEach(
            metricName ->
                metricNameLock.computeIfAbsent(metricName, n -> new ReentrantLock()).lock());
  }

  private void releaseLocks(Collection<Metric> metrics) {
    metrics.stream()
        .map(Metric::getName)
        .distinct()
        .sorted()
        .forEach(metricName -> metricNameLock.get(metricName).unlock());
  }

  private Optional<NamedMetricStore> get(String name) {
    return Optional.ofNullable(metricsByKey.get(name));
  }

  private Stream<NamedMetricStore> get(MetricFilter filter) {
    Set<String> names = getNames(filter);
    return metricsByKey.entrySet().stream()
        .filter(e -> names.contains(e.getKey()))
        .map(Entry::getValue);
  }

  private Set<String> getNames(MetricFilter filter) {
    Set<String> names = new HashSet<>();
    if (CollectionUtils.isNotEmpty(filter.getMetricNames())) {
      names.addAll(filter.getMetricNames());
    }
    if (CollectionUtils.isNotEmpty(filter.getSourceKeys())) {
      names.addAll(
          filter.getSourceKeys().stream()
              .map(MetricSourceKey::getName)
              .collect(Collectors.toList()));
    }
    if (CollectionUtils.isNotEmpty(filter.getKeys())) {
      names.addAll(
          filter.getKeys().stream()
              .map(MetricKey::getSourceKey)
              .map(MetricSourceKey::getName)
              .collect(Collectors.toList()));
    }
    if (CollectionUtils.isEmpty(names)) {
      names = new HashSet<>(metricsByKey.keySet());
    }
    return names;
  }

  private void save(Metric metric) {
    if (metric.getSourceUuid() != null) {
      MetricSourceState metricSourceState =
          sourceStateMap.getOrDefault(
              new Pair<>(metric.getCustomerUUID(), metric.getSourceUuid()),
              MetricSourceState.ACTIVE);
      PlatformMetrics platformMetric = PlatformMetrics.fromMetricName(metric.getName());
      // All metrics, collected through YB Anywhere as a backup scenario from external systems
      // - are only valid for active source
      boolean validForSourceState =
          platformMetric != null
              ? platformMetric.getValidForSourceStates().contains(metricSourceState)
              : metricSourceState == MetricSourceState.ACTIVE;
      if (!validForSourceState) {
        log.debug(
            "Skipping metric {} from source {} as it's marked {}",
            metric.getName(),
            metric.getSourceUuid(),
            metricSourceState.name());
        return;
      }
    }

    NamedMetricStore store =
        metricsByKey.computeIfAbsent(metric.getName(), n -> new NamedMetricStore());

    store.save(metric);
  }

  private static UUID getUuidKey(UUID uuid) {
    return uuid != null ? uuid : NULL_UUID;
  }

  @Value
  private static class NamedMetricStore {
    Map<UUID, CustomerMetricStore> customerMetrics = new HashMap<>();

    private Optional<CustomerMetricStore> get(UUID uuid) {
      return Optional.ofNullable(customerMetrics.get(getUuidKey(uuid)));
    }

    private Stream<CustomerMetricStore> get(MetricFilter filter) {
      Set<UUID> uuids = new HashSet<>();
      if (filter.getCustomerUuid() != null) {
        uuids.add(filter.getCustomerUuid());
      }
      if (CollectionUtils.isNotEmpty(filter.getSourceKeys())) {
        uuids.addAll(
            filter.getSourceKeys().stream()
                .map(MetricSourceKey::getCustomerUuid)
                .filter(Objects::nonNull)
                .collect(Collectors.toList()));
      }
      if (CollectionUtils.isNotEmpty(filter.getKeys())) {
        uuids.addAll(
            filter.getKeys().stream()
                .map(MetricKey::getSourceKey)
                .map(MetricSourceKey::getCustomerUuid)
                .filter(Objects::nonNull)
                .collect(Collectors.toList()));
      }

      return customerMetrics.entrySet().stream()
          .filter(e -> CollectionUtils.isEmpty(uuids) || uuids.contains(e.getKey()))
          .map(Entry::getValue);
    }

    private void save(Metric metric) {
      customerMetrics
          .computeIfAbsent(getUuidKey(metric.getCustomerUUID()), k -> new CustomerMetricStore())
          .save(metric);
    }
  }

  @Value
  private static class CustomerMetricStore {
    Map<UUID, SourceMetricStore> sourceMetrics = new HashMap<>();

    private Optional<SourceMetricStore> get(UUID uuid) {
      return Optional.ofNullable(sourceMetrics.get(getUuidKey(uuid)));
    }

    private Stream<SourceMetricStore> get(MetricFilter filter) {
      Set<UUID> uuids = new HashSet<>();
      if (filter.getSourceUuid() != null) {
        uuids.add(filter.getSourceUuid());
      }
      if (CollectionUtils.isNotEmpty(filter.getSourceKeys())) {
        uuids.addAll(
            filter.getSourceKeys().stream()
                .map(MetricSourceKey::getSourceUuid)
                .filter(Objects::nonNull)
                .collect(Collectors.toList()));
      }
      if (CollectionUtils.isNotEmpty(filter.getKeys())) {
        uuids.addAll(
            filter.getKeys().stream()
                .map(MetricKey::getSourceKey)
                .map(MetricSourceKey::getSourceUuid)
                .filter(Objects::nonNull)
                .collect(Collectors.toList()));
      }

      return sourceMetrics.entrySet().stream()
          .filter(e -> CollectionUtils.isEmpty(uuids) || uuids.contains(e.getKey()))
          .map(Entry::getValue);
    }

    private void save(Metric metric) {
      sourceMetrics
          .computeIfAbsent(getUuidKey(metric.getSourceUuid()), k -> new SourceMetricStore())
          .save(metric);
    }
  }

  @Value
  private static class SourceMetricStore {
    List<Metric> sourceMetrics = new ArrayList<>();

    private Optional<Metric> get(Map<String, String> labels) {
      return sourceMetrics.stream()
          .filter(
              metric ->
                  labels.entrySet().stream()
                      .allMatch(
                          l -> Objects.equals(metric.getLabelValue(l.getKey()), l.getValue())))
          .findFirst();
    }

    private Stream<Metric> get(MetricFilter filter) {
      return sourceMetrics.stream().filter(filter::match);
    }

    private void save(Metric metric) {
      Metric existing = get(metric.getKeyLabelValues()).orElse(null);
      if (existing != null) {
        existing.setValue(metric.getValue());
        existing.setUpdateTime(metric.getUpdateTime());
        existing.setExpireTime(metric.getExpireTime());
        existing.setDeleted(false);
      } else {
        sourceMetrics.add(metric);
      }
    }
  }
}
