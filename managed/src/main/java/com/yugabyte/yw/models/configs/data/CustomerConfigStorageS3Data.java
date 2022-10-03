// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.configs.data;

import com.fasterxml.jackson.annotation.JsonProperty;
import io.swagger.annotations.ApiModelProperty;
import java.util.List;
import javax.validation.Valid;
import javax.validation.constraints.Size;

public class CustomerConfigStorageS3Data extends CustomerConfigStorageData {
  @ApiModelProperty(value = "AWS access key identifier", example = "AAA....ZZZ")
  @JsonProperty("AWS_ACCESS_KEY_ID")
  @Size(min = 1)
  public String awsAccessKeyId;

  @ApiModelProperty(value = "AWS secret access key", example = "ZaDF....RPZ")
  @JsonProperty("AWS_SECRET_ACCESS_KEY")
  @Size(min = 1)
  public String awsSecretAccessKey;

  @ApiModelProperty(value = "AWS host base", example = "s3.amazonaws.com")
  @JsonProperty("AWS_HOST_BASE")
  public String awsHostBase;

  @ApiModelProperty(value = "path style access boolean")
  @JsonProperty("PATH_STYLE_ACCESS")
  public boolean isPathStyleAccess = false;

  @ApiModelProperty(value = "IAM Instance profile")
  @JsonProperty("IAM_INSTANCE_PROFILE")
  public boolean isIAMInstanceProfile = false;

  @Valid
  @ApiModelProperty(value = "Region locations for multi-region backups")
  @JsonProperty("REGION_LOCATIONS")
  public List<RegionLocations> regionLocations;

  public static class RegionLocations extends RegionLocationsBase {
    @ApiModelProperty(value = "AWS host base", example = "s3.amazonaws.com")
    @JsonProperty("AWS_HOST_BASE")
    public String awsHostBase;
  }
}
