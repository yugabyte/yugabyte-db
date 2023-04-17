// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.models.UniversePerfAdvisorRun;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import lombok.Data;
import lombok.EqualsAndHashCode;
import lombok.experimental.Accessors;

@ApiModel
@Data
@EqualsAndHashCode(callSuper = true)
@Accessors(chain = true)
public class PerfAdvisorManualRunStatus extends YBPSuccess {

  public PerfAdvisorManualRunStatus(boolean success, String message) {
    super(success, message);
  }

  @ApiModelProperty(value = "Started or in-progress perf advisor run")
  private UniversePerfAdvisorRun activeRun;
}
