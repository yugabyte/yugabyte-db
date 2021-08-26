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

import com.yugabyte.yw.models.MetricKey;
import com.yugabyte.yw.models.MetricSourceKey;
import com.yugabyte.yw.models.helpers.PlatformMetrics;
import java.util.ArrayList;
import java.util.Collection;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import lombok.Builder;
import lombok.NonNull;
import lombok.Value;

@Value
@Builder
public class MetricFilter {
  Set<UUID> uuids;
  UUID customerUuid;
  UUID sourceUuid;
  List<PlatformMetrics> metrics;
  Set<MetricSourceKey> sourceKeys;
  Set<MetricKey> keys;
  Boolean expired;

  public static class MetricFilterBuilder {
    Set<UUID> uuids = new HashSet<>();
    List<PlatformMetrics> metrics = new ArrayList<>();
    Set<MetricSourceKey> sourceKeys = new HashSet<>();
    Set<MetricKey> keys = new HashSet<>();

    public MetricFilterBuilder uuids(@NonNull Collection<UUID> uuids) {
      this.uuids.addAll(uuids);
      return this;
    }

    public MetricFilterBuilder uuid(@NonNull UUID uuid) {
      this.uuids.add(uuid);
      return this;
    }

    public MetricFilterBuilder customerUuid(@NonNull UUID customerUuid) {
      this.customerUuid = customerUuid;
      return this;
    }

    public MetricFilterBuilder sourceUuid(@NonNull UUID sourceUuid) {
      this.sourceUuid = sourceUuid;
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

    public MetricFilterBuilder expireTime(@NonNull Boolean expired) {
      this.expired = expired;
      return this;
    }
  }
}
