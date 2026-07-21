package com.yugabyte.yw.models.helpers.exporters.server;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_WRITE;

import com.fasterxml.jackson.annotation.JsonIgnore;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.List;
import javax.validation.constraints.DecimalMax;
import javax.validation.constraints.DecimalMin;
import javax.validation.constraints.NotNull;
import lombok.Data;
import org.apache.commons.collections4.CollectionUtils;

/**
 * yb-master glog export configuration. Internal-only: exposed solely through the unified
 * export-telemetry-configs API. Sibling of audit/query/metrics configs; owns its exporter list so
 * master logs can target sinks independently of other log types.
 */
@Data
@ApiModel(description = "Master Log Configuration")
public class MasterLogConfig {

  @NotNull
  @ApiModelProperty(value = "Master logs exporter config", accessMode = READ_WRITE)
  private List<UniverseServerLogsExporterConfig> universeLogsExporterConfig;

  @ApiModelProperty(
      value = "Minimum yb-master glog severity to export. Lines below this level are dropped.",
      accessMode = READ_WRITE)
  private MasterLogLevel minLevel = MasterLogLevel.INFO;

  @DecimalMin("0.0")
  @DecimalMax("1.0")
  @ApiModelProperty(
      value =
          "Fraction (0.0-1.0) of high-volume, low-value noise log lines to drop. Defaults to 0.99"
              + " (drop 99%). Set to 0.0 to keep all such lines.",
      accessMode = READ_WRITE)
  private Double noiseSampleDropRatio = 0.99;

  /** Export is active when at least one exporter is configured (no separate enable flag). */
  @JsonIgnore
  public boolean isExportActive() {
    return CollectionUtils.isNotEmpty(universeLogsExporterConfig);
  }
}
