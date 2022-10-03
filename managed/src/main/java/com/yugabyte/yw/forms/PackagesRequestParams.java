// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.yugabyte.yw.cloud.PublicCloudConstants.OsType;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import play.data.validation.Constraints;

@ApiModel(description = "Packages request parameters")
public class PackagesRequestParams {

  public enum ArchitectureType {
    AARCH64,
    X86_64
  }

  @Constraints.Required
  @ApiModelProperty(value = "Build number", required = true)
  public String buildNumber;

  @ApiModelProperty(value = "OS Type")
  public OsType osType = OsType.LINUX;

  @ApiModelProperty(value = "Architecture Type")
  public ArchitectureType architectureType = ArchitectureType.X86_64;

  @ApiModelProperty(value = "Package name")
  public String packageName = "ybc";

  @ApiModelProperty(value = "Archive Type")
  public String archiveType = "tar.gz";
}
