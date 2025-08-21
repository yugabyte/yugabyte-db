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
}
