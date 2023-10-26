// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.configs.data;

import com.fasterxml.jackson.annotation.JsonProperty;
import io.swagger.annotations.ApiModelProperty;
import java.util.List;
import javax.annotation.Nullable;
import javax.validation.Valid;
import javax.validation.constraints.Size;

public class CustomerConfigStorageGCSData extends CustomerConfigStorageData {
  @ApiModelProperty(value = "GCS credentials json")
  @JsonProperty("GCS_CREDENTIALS_JSON")
  @Nullable
  @Size(min = 2)
  public String gcsCredentialsJson;

  @ApiModelProperty(
      value = "Boolean flag showing whether to use GCP IAM or not for the storage config.")
  @JsonProperty("USE_GCP_IAM")
  public boolean useGcpIam = false;

  @Valid
  @ApiModelProperty(value = "Region locations for multi-region backups")
  @JsonProperty("REGION_LOCATIONS")
  public List<RegionLocations> regionLocations;

  public static class RegionLocations extends RegionLocationsBase {}
}
