// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import lombok.Data;
import lombok.experimental.Accessors;

/** Universe perf advisor settings. Fields are nullable - which means use default global value */
@ApiModel
@Data
@Accessors(chain = true)
public class PerfAdvisorSettingsWithDefaults {

  @ApiModelProperty(value = "Universe custom settings")
  private PerfAdvisorSettingsFormData universeSettings;

  @ApiModelProperty(value = "Universe custom settings")
  private PerfAdvisorSettingsFormData defaultSettings;
}
