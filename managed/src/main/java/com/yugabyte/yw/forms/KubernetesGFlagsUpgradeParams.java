package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.databind.annotation.JsonDeserialize;
import com.yugabyte.yw.common.operator.KubernetesResourceDetails;
import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.common.YbaApi.YbaApiVisibility;
import io.swagger.annotations.ApiModelProperty;
import lombok.Getter;
import lombok.Setter;

@JsonIgnoreProperties(ignoreUnknown = true)
@JsonDeserialize(converter = KubernetesGFlagsUpgradeParams.Converter.class)
public class KubernetesGFlagsUpgradeParams extends GFlagsUpgradeParams {
  @YbaApi(visibility = YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.21.0.0")
  @ApiModelProperty(value = "YbaApi Internal", hidden = true)
  @Getter
  @Setter
  private KubernetesResourceDetails kubernetesResourceDetails;

  public static class Converter extends BaseConverter<KubernetesGFlagsUpgradeParams> {}
}
