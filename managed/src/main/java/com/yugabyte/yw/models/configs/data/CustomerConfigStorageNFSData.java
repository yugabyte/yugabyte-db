// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.configs.data;

import com.fasterxml.jackson.annotation.JsonProperty;
import io.swagger.annotations.ApiModelProperty;
import java.util.List;
import javax.validation.Valid;

public class CustomerConfigStorageNFSData extends CustomerConfigStorageData {
  @Valid
  @ApiModelProperty(value = "Region locations for multi-region backups")
  @JsonProperty("REGION_LOCATIONS")
  public List<RegionLocations> regionLocations;

  @ApiModelProperty(value = "NFS bucket")
  @JsonProperty("NFS_BUCKET")
  public String nfsBucket = "yugabyte_backup";

  public static class RegionLocations extends RegionLocationsBase {
    @ApiModelProperty(value = "NFS bucket")
    @JsonProperty("NFS_BUCKET")
    public String nfsBucket = "yugabyte_backup";
  }
}
