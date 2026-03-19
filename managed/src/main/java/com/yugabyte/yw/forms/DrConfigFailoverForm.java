package com.yugabyte.yw.forms;

import com.yugabyte.yw.common.operator.KubernetesResourceDetails;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.Map;
import java.util.UUID;
import lombok.Getter;
import lombok.Setter;
import lombok.ToString;
import play.data.validation.Constraints.Required;

@ApiModel(description = "drConfig failover form")
@ToString
public class DrConfigFailoverForm {

  @ApiModelProperty(value = "New primary universe UUID")
  @Required
  public UUID primaryUniverseUuid;

  @ApiModelProperty(value = "New dr replica universe UUID")
  @Required
  public UUID drReplicaUniverseUuid;

  /**
   * @deprecated Do not pass in this field. This field is kept for backward compatibility and if it
   *     is not passed in, the failover task will compute it which potentially can be at a later
   *     time and hence reduce the data loss.
   */
  @ApiModelProperty(
      value =
          "A map from database ID to its safetime since epoch in micro-seconds to use "
              + "during unplanned failover")
  @Deprecated
  public Map<String, Long> namespaceIdSafetimeEpochUsMap;

  @ApiModelProperty(hidden = true)
  @Getter
  @Setter
  private KubernetesResourceDetails kubernetesResourceDetails;
}
