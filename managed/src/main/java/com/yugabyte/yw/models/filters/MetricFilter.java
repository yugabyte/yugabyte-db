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
import lombok.Builder;
import lombok.NonNull;
import lombok.Value;

import java.util.ArrayList;
import java.util.Collection;
import java.util.List;
import java.util.UUID;

@Value
@Builder
public class MetricFilter {
  Long id;
  UUID customerUuid;
  UUID targetUuid;
  List<MetricKey> keys;
  Boolean expired;

  public static class MetricFilterBuilder {
    List<MetricKey> keys = new ArrayList<>();

    public MetricFilterBuilder id(@NonNull Long id) {
      this.id = id;
      return this;
    }

    public MetricFilterBuilder customerUuid(@NonNull UUID customerUuid) {
      this.customerUuid = customerUuid;
      return this;
    }

    public MetricFilterBuilder targetUuid(@NonNull UUID targetUuid) {
      this.targetUuid = targetUuid;
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
