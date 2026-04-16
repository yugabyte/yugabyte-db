/*
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.common.metrics;

import static com.yugabyte.yw.common.metrics.MetricService.DEFAULT_METRIC_EXPIRY_SEC;
import static com.yugabyte.yw.common.metrics.MetricService.buildMetricTemplate;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.utils.CapacityReservationUtil;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.filters.MetricFilter;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import com.yugabyte.yw.models.helpers.PlatformMetrics;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.Set;
import lombok.extern.slf4j.Slf4j;

@Singleton
@Slf4j
public class ProviderMetricProvider implements MetricsProvider {
  private static final Set<Common.CloudType> CAPACITY_RESERVATION_TYPES =
      Set.of(Common.CloudType.aws, Common.CloudType.azu, Common.CloudType.gcp);

  @Inject RuntimeConfGetter confGetter;

  @Override
  public List<MetricSaveGroup> getMetricGroups() throws Exception {
    List<MetricSaveGroup> metricSaveGroups = new ArrayList<>();

    for (Provider provider : Provider.getAll()) {
      if (!CAPACITY_RESERVATION_TYPES.contains(provider.getCloudCode())) {
        continue;
      }
      boolean enabled = CapacityReservationUtil.isReservationEnabled(confGetter, provider);

      MetricSaveGroup providerGroup =
          MetricSaveGroup.builder()
              .metric(
                  buildMetricTemplate(
                          PlatformMetrics.CAPACITY_RESERVATION_ENABLED_STATUS,
                          DEFAULT_METRIC_EXPIRY_SEC)
                      .setSourceUuid(provider.getUuid())
                      .setValue(statusValue(enabled))
                      .setLabels(
                          Map.of(
                              KnownAlertLabels.SOURCE_UUID.labelName(),
                              provider.getUuid().toString(),
                              KnownAlertLabels.SOURCE_TYPE.labelName(),
                              "provider",
                              KnownAlertLabels.CLOUD_TYPE.labelName(),
                              provider.getCloudCode().name())))
              .cleanMetricFilter(
                  MetricFilter.builder()
                      .metricNames(
                          Collections.singleton(
                              PlatformMetrics.CAPACITY_RESERVATION_ENABLED_STATUS))
                      .sourceUuid(provider.getUuid())
                      .build())
              .build();

      metricSaveGroups.add(providerGroup);
    }
    return metricSaveGroups;
  }

  @Override
  public String getName() {
    return "Provider metrics";
  }
}
