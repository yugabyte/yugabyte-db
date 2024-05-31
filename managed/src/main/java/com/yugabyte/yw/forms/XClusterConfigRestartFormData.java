// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.forms;

import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.Set;
import javax.validation.Valid;
import lombok.ToString;
import play.data.validation.Constraints;

@ApiModel(description = "xcluster restart form")
@ToString
public class XClusterConfigRestartFormData {

  @ApiModelProperty(
      value = "Source Universe table IDs; if empty, the whole config will restart",
      example = "[\"000033df000030008000000000004006\", \"000033df00003000800000000000400b\"]",
      required = true)
  public Set<String> tables;

  @Valid
  @Constraints.Required
  @ApiModelProperty("Parameters needed for the bootstrap flow including backup/restore")
  public RestartBootstrapParams bootstrapParams;

  @ApiModelProperty("Run the pre-checks without actually running the subtasks")
  public boolean dryRun = false;

  @ApiModel(description = "Bootstrap parameters for restarting")
  @ToString
  public static class RestartBootstrapParams {
    @Constraints.Required
    @ApiModelProperty(value = "Parameters used to do Backup/restore", required = true)
    public XClusterConfigCreateFormData.BootstrapParams.BootstarpBackupParams backupRequestParams;
  }
}
