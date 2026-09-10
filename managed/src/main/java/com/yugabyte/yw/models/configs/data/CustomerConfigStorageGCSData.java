// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.models.configs.data;

import com.fasterxml.jackson.annotation.JsonIgnore;
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

  /**
   * @deprecated Federation no longer takes the external_account JSON here; the DB node renders it
   *     from the provider's federated IAM audience. Kept only for backward-compatible
   *     deserialization; not read anywhere.
   */
  @Deprecated
  @ApiModelProperty(
      hidden = true,
      value = "Deprecated: superseded by the provider-level GCP audience")
  @JsonProperty("EXTERNAL_ACCOUNT_CONFIG")
  @Nullable
  public String externalAccountConfig;

  /**
   * @deprecated The IMDSv2 token URL is now always emitted by the on-node template. Retained only
   *     for backward-compatible deserialization; not read anywhere.
   */
  @Deprecated
  @ApiModelProperty(hidden = true, value = "Deprecated: IMDSv2 token URL is always emitted on-node")
  @JsonProperty("USE_IMDSV2")
  public boolean useImdsv2 = false;

  @Valid
  @ApiModelProperty(value = "Region locations for multi-region backups")
  @JsonProperty("REGION_LOCATIONS")
  public List<RegionLocations> regionLocations;

  // Transient (never persisted): the GCP Workload Identity Federation audience resolved from the
  // universe's provider at backup/delete time. When set on a useGcpIam config, YBA builds an
  // external_account (WIF) Storage client in-process instead of using Application Default creds.
  @JsonIgnore @Nullable public String federationAudience;

  public static class RegionLocations extends RegionLocationsBase {}
}
