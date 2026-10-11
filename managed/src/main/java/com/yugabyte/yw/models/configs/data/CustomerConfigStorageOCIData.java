// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.models.configs.data;

import com.fasterxml.jackson.annotation.JsonProperty;
import io.swagger.annotations.ApiModelProperty;
import java.util.List;
import javax.annotation.Nullable;
import javax.validation.Valid;
import javax.validation.constraints.NotNull;
import javax.validation.constraints.Size;

public class CustomerConfigStorageOCIData extends CustomerConfigStorageData {
  @ApiModelProperty(
      value =
          "OCI region for Object Storage. Required for native IAM configs and used as the signing"
              + " region for S3-compatible static credential configs.",
      example = "us-sanjose-1")
  @JsonProperty("OCI_REGION")
  @NotNull
  public String ociRegion;

  @ApiModelProperty(value = "OCI Object Storage namespace. Required for native IAM configs.")
  @JsonProperty("OCI_NAMESPACE")
  @Nullable
  public String ociNamespace;

  @ApiModelProperty(
      value =
          "Boolean flag showing whether to use OCI IAM for native Object Storage. When false,"
              + " OCI S3-compatible static credentials must be provided instead.")
  @JsonProperty("USE_OCI_IAM")
  public boolean useOciIam = false;

  @ApiModelProperty(
      value = "OCI Customer Secret Key access key for S3-compatible Object Storage API")
  @JsonProperty("OCI_S3_ACCESS_KEY_ID")
  @Nullable
  @Size(min = 1)
  public String ociS3AccessKeyId;

  @ApiModelProperty(value = "OCI Customer Secret Key secret for S3-compatible Object Storage API")
  @JsonProperty("OCI_S3_SECRET_ACCESS_KEY")
  @Nullable
  @Size(min = 1)
  public String ociS3SecretAccessKey;

  @ApiModelProperty(
      value =
          "OCI S3-compatible API host base, e.g."
              + " namespace.compat.objectstorage.us-sanjose-1.oraclecloud.com")
  @JsonProperty("OCI_S3_HOST_BASE")
  @Nullable
  public String ociS3HostBase;

  @Valid
  @ApiModelProperty(value = "Region locations for multi-region backups")
  @JsonProperty("REGION_LOCATIONS")
  public List<RegionLocations> regionLocations;

  public static class RegionLocations extends RegionLocationsBase {
    @ApiModelProperty(
        value = "OCI S3-compatible API host base for the region",
        example = "namespace.compat.objectstorage.us-sanjose-1.oraclecloud.com")
    @JsonProperty("OCI_S3_HOST_BASE")
    @Nullable
    public String ociS3HostBase;
  }
}
