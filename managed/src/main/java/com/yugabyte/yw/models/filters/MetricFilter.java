/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.models.filters;

import com.yugabyte.yw.models.Metric;
import com.yugabyte.yw.models.MetricKey;
import com.yugabyte.yw.models.MetricSourceKey;
import com.yugabyte.yw.models.helpers.PlatformMetrics;
import java.util.Collection;
import java.util.Date;
import java.util.HashSet;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.Builder;
import lombok.NonNull;
import lombok.Value;
import org.apache.commons.collections.CollectionUtils;

@Value
@Builder
public class MetricFilter {
  UUID customerUuid;
  UUID sourceUuid;
  Set<String> metricNames;
  Set<MetricSourceKey> sourceKeys;
  Set<MetricKey> keys;
  Set<MetricKey> keysExcluded;
  Boolean expired;

  // Can't use @Builder(toBuilder = true) as it sets null fields as well, which breaks non null
  // checks.
  public MetricFilterBuilder toBuilder() {
    MetricFilterBuilder result = MetricFilter.builder();
    if (customerUuid != null) {
      result.customerUuid(customerUuid);
    }
    if (sourceUuid != null) {
      result.sourceUuid(sourceUuid);
    }
    if (metricNames != null) {
      result.metricNamesStr(metricNames);
    }
    if (sourceKeys != null) {
      result.sourceKeys(sourceKeys);
    }
    if (keys != null) {
      result.keys(keys);
    }
    if (keysExcluded != null) {
      result.keysExcluded(keysExcluded);
    }
    if (expired != null) {
      result.expired(expired);
    }
    return result;
  }

  public static class MetricFilterBuilder {
    Set<String> metricNames = new HashSet<>();
    Set<MetricSourceKey> sourceKeys = new HashSet<>();
    Set<MetricKey> keys = new HashSet<>();
    Set<MetricKey> keysExcluded = new HashSet<>();

    public MetricFilterBuilder customerUuid(@NonNull UUID customerUuid) {
      this.customerUuid = customerUuid;
      return this;
    }

    public MetricFilterBuilder sourceUuid(@NonNull UUID sourceUuid) {
      this.sourceUuid = sourceUuid;
      return this;
    }

    public MetricFilterBuilder metricNames(@NonNull Collection<PlatformMetrics> platformMetrics) {
      this.metricNames.addAll(
          platformMetrics.stream()
              .map(PlatformMetrics::getMetricName)
              .collect(Collectors.toList()));
      return this;
    }

    public MetricFilterBuilder metricNamesStr(@NonNull Collection<String> names) {
      this.metricNames.addAll(names);
      return this;
    }

    public MetricFilterBuilder metricName(@NonNull PlatformMetrics platformMetric) {
      this.metricNames.add(platformMetric.getMetricName());
      return this;
    }

    public MetricFilterBuilder sourceKeys(@NonNull Collection<MetricSourceKey> sourceKeys) {
      this.sourceKeys.addAll(sourceKeys);
      return this;
    }

    public MetricFilterBuilder sourceKeys(@NonNull MetricSourceKey sourceKey) {
      this.sourceKeys.add(sourceKey);
      return this;
    }

    public MetricFilterBuilder keys(@NonNull Collection<MetricKey> keys) {
      this.keys.addAll(keys);
      return this;
    }

    public MetricFilterBuilder key(@NonNull MetricKey key) {
      this.keys.add(key);
      return this;
    }

    public MetricFilterBuilder keysExcluded(@NonNull Collection<MetricKey> keysExcluded) {
      this.keysExcluded.addAll(keysExcluded);
      return this;
    }

    public MetricFilterBuilder keyExcluded(@NonNull MetricKey keyExcluded) {
      this.keysExcluded.add(keyExcluded);
      return this;
    }

    public MetricFilterBuilder expireTime(@NonNull Boolean expired) {
      this.expired = expired;
      return this;
    }
  }

  public boolean match(Metric metric) {
    if (customerUuid != null && !customerUuid.equals(metric.getCustomerUUID())) {
      return false;
    }
    if (sourceUuid != null && !sourceUuid.equals(metric.getSourceUuid())) {
      return false;
    }
    if (CollectionUtils.isNotEmpty(metricNames) && !metricNames.contains(metric.getName())) {
      return false;
    }
    MetricKey metricKey = MetricKey.from(metric);
    if (CollectionUtils.isNotEmpty(sourceKeys) && !sourceKeys.contains(metricKey.getSourceKey())) {
      return false;
    }
    if (CollectionUtils.isNotEmpty(keys) && !keys.contains(metricKey)) {
      return false;
    }
    if (CollectionUtils.isNotEmpty(keysExcluded) && keysExcluded.contains(metricKey)) {
      return false;
    }
    if (expired != null) {
      if (expired && metric.getExpireTime().after(new Date())) {
        return false;
      }
      if (!expired && metric.getExpireTime().before(new Date())) {
        return false;
      }
    }
    return !metric.isDeleted();
  }
}
