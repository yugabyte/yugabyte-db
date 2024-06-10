package com.yugabyte.yw.forms;

import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.Set;
import play.data.validation.Constraints.Required;

@ApiModel(description = "dr config set databases form")
public class DrConfigSetDatabasesForm {
  @ApiModelProperty(
      value = "Source universe database IDs",
      example = "[\"000033df000030008000000000004006\", \"000033df00003000800000000000400b\"]")
  @Required
  public Set<String> databases;
}
