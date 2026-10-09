package com.yugabyte.yw.models.helpers.exporters.server;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_WRITE;

import com.fasterxml.jackson.annotation.JsonIgnore;
import io.swagger.annotations.ApiModelProperty;
import java.util.List;
import javax.validation.constraints.NotNull;
import lombok.Data;
import org.apache.commons.collections4.CollectionUtils;

/**
 * Shared base for the internal-only diagnostic server-log export configs (YSQL Connection Manager,
 * node-agent, YNP, YB-Controller). Each is a plain file-tail export that owns its own exporter list
 * so it can target sinks independently of other log types. Unlike master and tserver logs there is
 * no {@code minLevel} knob: these are low-volume diagnostic sources exported in full.
 *
 * <p>Kubernetes availability differs by source (see {@link
 * com.yugabyte.yw.models.helpers.telemetry.ExportType}): YSQL Connection Manager and YB-Controller
 * are pod-local (supported on VM and K8s); node-agent and YNP are VM-only (their logs do not exist
 * inside the DB pods, so they are rejected on K8s in ExportTelemetryConfigParams).
 */
@Data
public abstract class SimpleServerLogConfig {

  @NotNull
  @ApiModelProperty(value = "Log exporter config", accessMode = READ_WRITE)
  private List<UniverseServerLogsExporterConfig> universeLogsExporterConfig;

  /** Export is active when at least one exporter is configured (no separate enable flag). */
  @JsonIgnore
  public boolean isExportActive() {
    return CollectionUtils.isNotEmpty(universeLogsExporterConfig);
  }
}
