package com.yugabyte.yw.forms;

import com.yugabyte.yw.common.operator.KubernetesResourceDetails;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.UUID;
import lombok.Getter;
import lombok.Setter;
import lombok.ToString;
import play.data.validation.Constraints.Required;

@ApiModel(description = "drConfig failover form")
@ToString
public class DrConfigSwitchoverForm {

  @ApiModelProperty(value = "New primary universe UUID")
  @Required
  public UUID primaryUniverseUuid;

  @ApiModelProperty(value = "New dr replica universe UUID")
  @Required
  public UUID drReplicaUniverseUuid;

  @ApiModelProperty(hidden = true)
  @Getter
  @Setter
  private KubernetesResourceDetails kubernetesResourceDetails;
}
