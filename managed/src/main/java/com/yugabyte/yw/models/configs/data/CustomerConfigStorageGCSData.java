// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models.configs.data;

import com.fasterxml.jackson.annotation.JsonProperty;
import javax.validation.constraints.NotNull;
import javax.validation.constraints.Size;

public class CustomerConfigStorageGCSData extends CustomerConfigStorageWithRegionsData {
  @JsonProperty("GCS_CREDENTIALS_JSON")
  @NotNull
  @Size(min = 2)
  public String gcsCredentialsJson;
}
