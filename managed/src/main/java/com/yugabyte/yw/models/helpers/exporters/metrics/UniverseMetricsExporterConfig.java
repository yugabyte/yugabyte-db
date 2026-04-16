package com.yugabyte.yw.models.helpers.exporters.metrics;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_WRITE;

import com.yugabyte.yw.models.helpers.exporters.UniverseExporterConfig;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import lombok.Data;
import lombok.EqualsAndHashCode;

@Data
@EqualsAndHashCode(callSuper = true)
@ApiModel(description = "Metrics Exporter Configuration")
public class UniverseMetricsExporterConfig extends UniverseExporterConfig {

  @ApiModelProperty(
      value =
          "Maximum batch size for sending metrics. Can be customised to each exporter differently.",
      accessMode = READ_WRITE,
      example = "1000")
  private Integer sendBatchMaxSize = 1000;

  @ApiModelProperty(
      value = "Batch size for sending metrics. Can be customised to each exporter differently.",
      accessMode = READ_WRITE,
      example = "100")
  private Integer sendBatchSize = 100;

  @ApiModelProperty(
      value =
          "Batch timeout in seconds for sending metrics. Can be customised to each exporter"
              + " differently.",
      accessMode = READ_WRITE,
      example = "10")
  private Integer sendBatchTimeoutSeconds = 10;

  @ApiModelProperty(
      value =
          "Custom prefix to add to all exported metric names. Can be customised to each exporter"
              + " differently.",
      accessMode = READ_WRITE,
      example = "ybdb.")
  private String metricsPrefix = "";

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
