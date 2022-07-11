// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.configs.data;

import com.fasterxml.jackson.annotation.JsonProperty;
import javax.validation.constraints.NotNull;
import javax.validation.constraints.Size;

public class CustomerConfigStorageAzureData extends CustomerConfigStorageWithRegionsData {
  @JsonProperty("AZURE_STORAGE_SAS_TOKEN")
  @NotNull
  @Size(min = 4)
  public String azureSasToken;
}
