// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.configs.data;

import com.fasterxml.jackson.annotation.JsonProperty;
import io.swagger.annotations.ApiModelProperty;
import java.util.List;
import javax.validation.Valid;
import javax.validation.constraints.NotNull;
import javax.validation.constraints.Size;

public class CustomerConfigStorageWithRegionsData extends CustomerConfigStorageData {
  @ApiModelProperty(value = "Region locations for multi-region backups")
  @JsonProperty("REGION_LOCATIONS")
  @Valid
  public List<RegionLocation> regionLocations;

  public static class RegionLocation {
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
    @Size(min = 5)
    public String location;

    @ApiModelProperty(value = "AWS host base", example = "s3.amazonaws.com")
    @JsonProperty("AWS_HOST_BASE")
    public String awsHostBase;
  }
}
