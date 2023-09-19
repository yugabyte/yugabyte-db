package com.yugabyte.yw.forms;

import com.yugabyte.yw.common.operator.KubernetesResourceDetails;
import io.swagger.annotations.ApiModelProperty;
import lombok.Getter;
import lombok.Setter;

public class KubernetesUpgradeParams extends UpgradeParams {
  @ApiModelProperty(hidden = true)
  @Getter
  @Setter
  private KubernetesResourceDetails kubernetesResourceDetails;
}
