/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.metrics;

import java.util.Collections;
import java.util.Map;
import java.util.Set;
import lombok.Builder;
import lombok.Value;

@Value
@Builder(toBuilder = true)
public class MetricQueryContext {

  // Set in case we need query to be wrapped with topk or boottomk function
  @Builder.Default boolean topKQuery = false;
  // Filters, applied to each metric query
  @Builder.Default Map<String, String> additionalFilters = Collections.emptyMap();
  // Filters, applied to each metric query
  @Builder.Default Map<String, String> excludeFilters = Collections.emptyMap();
  // Group by, applied to each metric query
  @Builder.Default Set<String> additionalGroupBy = Collections.emptySet();
  // Group by, which need to be removed from original metric group by list
  @Builder.Default Set<String> removeGroupBy = Collections.emptySet();

  // Period, used in range queries, eg. (metric{labels}[60s]).
  int queryRangeSecs;
  // Period, used in range queries, eg. (metric{labels} @ 1609746000).
  Long queryTimestampSec;
}
