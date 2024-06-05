package com.yugabyte.yw.forms;

import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.Set;
import javax.validation.Valid;
import play.data.validation.Constraints.Required;

@ApiModel(description = "dr config set tables form")
public class DrConfigSetTablesForm {
  @ApiModelProperty(
      value = "Source universe table IDs",
      example = "[\"000033df000030008000000000004006\", \"000033df00003000800000000000400b\"]")
  @Required
  public Set<String> tables;

  @ApiModelProperty(
      value =
          "Whether or not YBA should also include all index tables from any provided main tables.")
  public boolean autoIncludeIndexTables;

  @Valid
  @ApiModelProperty("Parameters needed for the bootstrap flow including backup/restore")
  public XClusterConfigRestartFormData.RestartBootstrapParams bootstrapParams;
}
