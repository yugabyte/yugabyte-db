package com.yugabyte.yw.models.helpers.exporters.query;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_WRITE;

import com.yugabyte.yw.models.helpers.exporters.UniverseExporterConfig;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import lombok.Data;
import lombok.EqualsAndHashCode;

@Data
@EqualsAndHashCode(callSuper = true)
@ApiModel(description = "Universe Logs Exporter Config")
public class UniverseQueryLogsExporterConfig extends UniverseExporterConfig {
  @ApiModelProperty(value = "Maximum batch size for query logs exporter", accessMode = READ_WRITE)
  int sendBatchMaxSize = 1000;

  @ApiModelProperty(value = "Batch size for query logs exporter", accessMode = READ_WRITE)
  int sendBatchSize = 100;

  @ApiModelProperty(
      value = "Maximum batch timeout for query logs exporter in seconds",
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
