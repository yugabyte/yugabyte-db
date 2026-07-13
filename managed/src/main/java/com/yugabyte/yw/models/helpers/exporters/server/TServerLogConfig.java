package com.yugabyte.yw.models.helpers.exporters.server;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_WRITE;

import com.fasterxml.jackson.annotation.JsonIgnore;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.List;
import javax.validation.constraints.NotNull;
import lombok.Data;
import org.apache.commons.collections4.CollectionUtils;

/**
 * yb-tserver glog export configuration. Internal-only: exposed solely through the unified
 * export-telemetry-configs API. Sibling of audit/query/metrics/master configs; owns its exporter
 * list so tserver logs can target sinks independently of other log types.
 *
 * <p>Unlike master logs, the default {@code minLevel} is WARNING: yb-tserver INFO is very high
 * volume, so the pipeline drops it by default (customer can lower to INFO to keep everything). The
 * noise-sampling tiers are hardcoded (not a customer knob) - see the generator.
 */
@Data
@ApiModel(description = "TServer Log Configuration")
public class TServerLogConfig {

  @NotNull
  @ApiModelProperty(value = "TServer logs exporter config", accessMode = READ_WRITE)
  private List<UniverseServerLogsExporterConfig> universeLogsExporterConfig;

  @ApiModelProperty(
      value =
          "Minimum yb-tserver glog severity to export. Lines below this level are dropped."
              + " Defaults to WARNING (yb-tserver INFO is very high volume).",
      accessMode = READ_WRITE)
  private ServerLogLevel minLevel = ServerLogLevel.WARNING;

  /** Export is active when at least one exporter is configured (no separate enable flag). */
  @JsonIgnore
  public boolean isExportActive() {
    return CollectionUtils.isNotEmpty(universeLogsExporterConfig);
  }
}
