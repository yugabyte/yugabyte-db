package com.yugabyte.yw.models.helpers.audit;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_WRITE;

import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.List;
import javax.validation.constraints.NotNull;
import lombok.Data;

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
}
