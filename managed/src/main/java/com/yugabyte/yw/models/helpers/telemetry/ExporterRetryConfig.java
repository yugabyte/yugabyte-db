/*
 * Copyright 2022 YugabyteDB, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.models.helpers.telemetry;

import com.yugabyte.yw.common.audit.otel.OtelCollectorConfigFormat.RetryConfig;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import io.swagger.annotations.ApiModelProperty.AccessMode;
import lombok.AllArgsConstructor;
import lombok.Data;
import lombok.NoArgsConstructor;

/**
 * Optional retry configuration for telemetry exporters. When provided via the API, these values
 * override the default retry_on_failure settings in the OpenTelemetry collector config. Duration
 * strings use the same format as the Otel collector (e.g. "30s", "1m", "60m").
 */
@Data
@NoArgsConstructor
@AllArgsConstructor
@ApiModel(description = "Optional retry configuration for telemetry export (Otel retry_on_failure)")
public class ExporterRetryConfig {

  @ApiModelProperty(
      value = "Whether retry on failure is enabled",
      accessMode = AccessMode.READ_WRITE,
      example = "true")
  private Boolean enabled;

  @ApiModelProperty(
      value =
          "Initial interval between retries. Duration string (e.g. \"30s\", \"1m\"). Used as "
              + "initial_interval in Otel collector.",
      accessMode = AccessMode.READ_WRITE,
      example = "30s")
  private String initialInterval;

  @ApiModelProperty(
      value =
          "Maximum interval between retries. Duration string (e.g. \"10m\", \"1800m\"). Used as "
              + "max_interval in Otel collector.",
      accessMode = AccessMode.READ_WRITE,
      example = "10m")
  private String maxInterval;

  @ApiModelProperty(
      value =
          "Maximum elapsed time for all retries. Duration string (e.g. \"60m\", \"1800m\"). Used as"
              + " max_elapsed_time in Otel collector.",
      accessMode = AccessMode.READ_WRITE,
      example = "60m")
  private String maxElapsedTime;

  public static RetryConfig applyOverrides(RetryConfig retryConfig, ExporterRetryConfig apiRetry) {
    if (retryConfig != null && apiRetry != null) {
      if (apiRetry.getEnabled() != null) {
        retryConfig.setEnabled(apiRetry.getEnabled());
      }
      if (apiRetry.getInitialInterval() != null) {
        retryConfig.setInitial_interval(apiRetry.getInitialInterval());
      }
      if (apiRetry.getMaxInterval() != null) {
        retryConfig.setMax_interval(apiRetry.getMaxInterval());
      }
      if (apiRetry.getMaxElapsedTime() != null) {
        retryConfig.setMax_elapsed_time(apiRetry.getMaxElapsedTime());
      }
    }
    return retryConfig;
  }
}
