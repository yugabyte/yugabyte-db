// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.forms;

import io.ebean.annotation.EnumValue;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.List;
import play.data.validation.Constraints.Required;

@ApiModel(description = "isolated create backup form")
public class YbaBackupForm {

  public enum Component {
    @EnumValue("yba")
    YBA,

    @EnumValue("prometheus")
    PROMETHEUS
  }

  @Required
  @ApiModelProperty(value = "Local directory to store backup.tar.gz", required = true)
  public String localDir;

  @Required
  @ApiModelProperty(value = "YBA components to backup (yba, prom)", required = true)
  public List<Component> components;
}
