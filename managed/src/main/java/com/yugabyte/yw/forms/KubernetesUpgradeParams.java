package com.yugabyte.yw.forms;

import com.yugabyte.yw.common.operator.KubernetesResourceDetails;
import com.yugabyte.yw.models.common.YbaApi;
import io.swagger.annotations.ApiModelProperty;
import lombok.Getter;
import lombok.Setter;

public class KubernetesUpgradeParams extends UpgradeParams {
  @ApiModelProperty(hidden = true)
  @Getter
  @Setter
  private KubernetesResourceDetails kubernetesResourceDetails;

  @ApiModelProperty(value = "YbaApi Internal. Requested batch size values for rolling upgrade")
  @YbaApi(visibility = YbaApi.YbaApiVisibility.INTERNAL, sinceYBAVersion = "2024.2.0.0")
  public RollMaxBatchSize rollMaxBatchSize = null;
}
