package com.yugabyte.yw.models.helpers.exporters.audit;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_WRITE;

import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.List;
import javax.validation.constraints.NotNull;
import lombok.Data;
import org.apache.commons.collections4.CollectionUtils;

@Data
@ApiModel(description = "Audit Log Configuration")
public class AuditLogConfig {

  @ApiModelProperty(value = "YSQL audit config", accessMode = READ_WRITE)
  private YSQLAuditConfig ysqlAuditConfig;

  @ApiModelProperty(value = "YCQL audit config", accessMode = READ_WRITE)
  private YCQLAuditConfig ycqlAuditConfig;

  @NotNull
  @ApiModelProperty(value = "Universe logs exporter config", accessMode = READ_WRITE)
  private List<UniverseLogsExporterConfig> universeLogsExporterConfig;

  @ApiModelProperty(value = "Universe logs export active", accessMode = READ_WRITE)
  private boolean exportActive = true;

  /**
   * Keeps {@link #exportActive} consistent with the exporter list: export can only be active when
   * at least one exporter is configured. {@code exportActive} is a stored flag (unlike metrics,
   * whose isExportActive() is computed), so without this it can default/persist to true with an
   * empty list and the UI shows "export active" with no exporters. Idempotent; call before
   * persisting.
   */
  public void normalizeExportActive() {
    if (CollectionUtils.isEmpty(universeLogsExporterConfig)) {
      exportActive = false;
    }
  }
}
