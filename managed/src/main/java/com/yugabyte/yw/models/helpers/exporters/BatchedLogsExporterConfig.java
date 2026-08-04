package com.yugabyte.yw.models.helpers.exporters;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_WRITE;

import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.ToString;

/**
 * Batching and memory-limiter tunables shared by high-volume log exporters (query logs, server
 * logs). Extends {@link UniverseExporterConfig} for the exporter UUID and additional tags. Audit
 * logs intentionally do not use these and extend {@link UniverseExporterConfig} directly.
 */
@Data
@EqualsAndHashCode(callSuper = true)
@ToString(callSuper = true)
@ApiModel(description = "Batched Logs Exporter Config")
public class BatchedLogsExporterConfig extends UniverseExporterConfig {

  @ApiModelProperty(value = "Maximum batch size for logs exporter", accessMode = READ_WRITE)
  int sendBatchMaxSize = 1000;

  @ApiModelProperty(value = "Batch size for logs exporter", accessMode = READ_WRITE)
  int sendBatchSize = 100;

  @ApiModelProperty(
      value = "Maximum batch timeout for logs exporter in seconds",
      accessMode = READ_WRITE)
  Integer sendBatchTimeoutSeconds = 10;

  @ApiModelProperty(
      value = "Memory limit in MiB for the OpenTelemetry Collector process in the config file.",
      accessMode = READ_WRITE,
      example = "2048")
  private Integer memoryLimitMib = 2048;

  @ApiModelProperty(
      value = "Check interval in seconds for the MemoryLimiterProcessor.",
      accessMode = READ_WRITE,
      example = "10")
  private Integer memoryLimitCheckIntervalSeconds = 10;
}
