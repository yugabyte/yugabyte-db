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

import com.yugabyte.yw.models.Metric;
import com.yugabyte.yw.models.MetricKey;
import com.yugabyte.yw.models.filters.MetricFilter;
import java.util.ArrayList;
import java.util.Collection;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import java.util.stream.Collectors;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections.CollectionUtils;

@Singleton
@Slf4j
public class MetricStorage {

  private final Map<MetricKey, Metric> metricsByKey = new ConcurrentHashMap<>();
  private final Set<MetricKey> dirtyMetrics = Collections.newSetFromMap(new ConcurrentHashMap<>());
  private final Set<MetricKey> deletedMetrics =
      Collections.newSetFromMap(new ConcurrentHashMap<>());

  public Metric get(MetricKey key) {
    return metricsByKey.get(key);
  }

  public List<Metric> list(MetricFilter metricFilter) {
    Collection<Metric> candidates;
    if (CollectionUtils.isNotEmpty(metricFilter.getKeys())) {
      candidates =
          metricFilter
              .getKeys()
              .stream()
              .map(metricsByKey::get)
              .filter(Objects::nonNull)
              .collect(Collectors.toList());
    } else {
      candidates = new ArrayList<>(metricsByKey.values());
    }
    return candidates.stream().filter(metricFilter::match).collect(Collectors.toList());
  }

  void initialize(List<Metric> metrics) {
    metrics.forEach(
        metric -> {
          MetricKey key = MetricKey.from(metric);
          metricsByKey.put(key, metric);
        });
  }

  public List<Metric> save(List<Metric> metrics) {
    if (CollectionUtils.isEmpty(metrics)) {
      return metrics;
    }
    metrics.forEach(
        metric -> {
          MetricKey key = MetricKey.from(metric);
          metricsByKey.put(key, metric);
          dirtyMetrics.add(key);
          deletedMetrics.remove(key);
        });
    return metrics;
  }

  public int delete(Collection<Metric> metrics) {
    if (CollectionUtils.isEmpty(metrics)) {
      return 0;
    }
    return (int)
        metrics
            .stream()
            .filter(
                metric -> {
                  MetricKey key = MetricKey.from(metric);
                  boolean deleted = (metricsByKey.remove(key) != null);
                  dirtyMetrics.remove(key);
                  deletedMetrics.add(key);
                  return deleted;
                })
            .count();
  }

  public Set<MetricKey> getDirtyMetrics() {
    return dirtyMetrics;
  }

  public Set<MetricKey> getDeletedMetrics() {
    return deletedMetrics;
  }
}
