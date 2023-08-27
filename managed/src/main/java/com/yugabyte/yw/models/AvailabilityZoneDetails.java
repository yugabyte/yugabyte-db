package com.yugabyte.yw.models;

import com.fasterxml.jackson.annotation.JsonInclude;
import com.yugabyte.yw.models.helpers.provider.region.KubernetesRegionInfo;
import io.swagger.annotations.ApiModelProperty;
import lombok.Data;
import lombok.EqualsAndHashCode;

@Data
@EqualsAndHashCode(callSuper = false)
@JsonInclude(JsonInclude.Include.NON_NULL)
public class AvailabilityZoneDetails {

  @Data
  @JsonInclude(JsonInclude.Include.NON_NULL)
  public static class AZCloudInfo {
    @ApiModelProperty private KubernetesRegionInfo kubernetes;
  }

  @ApiModelProperty private AZCloudInfo cloudInfo = new AZCloudInfo();
}
