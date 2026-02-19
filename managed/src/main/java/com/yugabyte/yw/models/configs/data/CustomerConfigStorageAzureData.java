// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.models.configs.data;

import com.fasterxml.jackson.annotation.JsonProperty;
import io.swagger.annotations.ApiModelProperty;
import java.util.List;
import javax.annotation.Nullable;
import javax.validation.Valid;
import javax.validation.constraints.Size;

public class CustomerConfigStorageAzureData extends CustomerConfigStorageData {
  @ApiModelProperty(value = "Azure storage SAS token")
  @JsonProperty("AZURE_STORAGE_SAS_TOKEN")
  @Nullable
  @Size(min = 4)
  public String azureSasToken;

  @ApiModelProperty(
      value =
          "Boolean flag showing whether to use Azure managed identities or not for the storage"
              + " config.")
  @JsonProperty("USE_AZURE_IAM")
  public boolean useAzureIam = false;

  @Valid
  @ApiModelProperty(value = "Region locations for multi-region backups")
  @JsonProperty("REGION_LOCATIONS")
  public List<RegionLocations> regionLocations;

  public static class RegionLocations extends RegionLocationsBase {
    @JsonProperty("AZURE_STORAGE_SAS_TOKEN")
    @Nullable
    @Size(min = 4)
    public String azureSasToken;
  }
}
