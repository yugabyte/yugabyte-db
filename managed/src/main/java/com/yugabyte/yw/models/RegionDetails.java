package com.yugabyte.yw.models;

import com.fasterxml.jackson.annotation.JsonInclude;
import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.models.helpers.provider.region.AWSRegionCloudInfo;
import com.yugabyte.yw.models.helpers.provider.region.AzureRegionCloudInfo;
import com.yugabyte.yw.models.helpers.provider.region.GCPRegionCloudInfo;
import com.yugabyte.yw.models.helpers.provider.region.KubernetesRegionInfo;
import io.swagger.annotations.ApiModelProperty;
import lombok.Data;
import lombok.EqualsAndHashCode;

@Data
@EqualsAndHashCode(callSuper = false)
@JsonInclude(JsonInclude.Include.NON_NULL)
public class RegionDetails {

  @ApiModelProperty(hidden = true)
  @Deprecated
  public String sg_id; // Security group ID.

  @Deprecated
  @ApiModelProperty(hidden = true)
  public String vnet; // Vnet key.

  @Deprecated
  @ApiModelProperty(hidden = true)
  public Architecture arch; // ybImage architecture.

  @Data
  @JsonInclude(JsonInclude.Include.NON_NULL)
  public static class RegionCloudInfo {
    @ApiModelProperty public AWSRegionCloudInfo aws;
    @ApiModelProperty public AzureRegionCloudInfo azu;
    @ApiModelProperty public GCPRegionCloudInfo gcp;
    @ApiModelProperty public KubernetesRegionInfo kubernetes;
  }

  @ApiModelProperty public RegionCloudInfo cloudInfo;
}
