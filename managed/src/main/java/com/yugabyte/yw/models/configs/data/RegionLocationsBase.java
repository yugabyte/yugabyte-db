// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.configs.data;

import com.fasterxml.jackson.annotation.JsonProperty;
import com.yugabyte.yw.models.configs.validators.BackupLocationLengthConstraint;
import io.swagger.annotations.ApiModelProperty;
import javax.validation.constraints.NotNull;
import javax.validation.constraints.Size;

public abstract class RegionLocationsBase {
  @ApiModelProperty(value = "Region name", example = "eastus")
  @JsonProperty("REGION")
  @NotNull
  @Size(min = 1)
  public String region;

  @ApiModelProperty(
      value = "Bucket location for the region",
      example = "s3://backups.yugabyte.com/test/guest/eastus")
  @JsonProperty("LOCATION")
  @NotNull
  @BackupLocationLengthConstraint
  public String location;
}
