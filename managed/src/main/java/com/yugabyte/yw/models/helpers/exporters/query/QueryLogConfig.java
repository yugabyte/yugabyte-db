package com.yugabyte.yw.models.helpers.exporters.query;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_WRITE;

import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.List;
import javax.validation.constraints.NotNull;
import lombok.Data;

@Data
@ApiModel(description = "Query Log Configuration")
public class QueryLogConfig {

  @ApiModelProperty(value = "YSQL query config", accessMode = READ_WRITE)
  private YSQLQueryLogConfig ysqlQueryLogConfig;

  @NotNull
  @ApiModelProperty(value = "Universe query logs exporter config", accessMode = READ_WRITE)
  private List<UniverseQueryLogsExporterConfig> universeLogsExporterConfig;

  @ApiModelProperty(value = "Universe logs export active", accessMode = READ_WRITE)
  private boolean exportActive = true;
}
