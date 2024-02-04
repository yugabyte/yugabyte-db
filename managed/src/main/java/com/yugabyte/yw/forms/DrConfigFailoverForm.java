package com.yugabyte.yw.forms;

import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.Map;
import java.util.UUID;
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

  @ApiModelProperty(
      value =
          "A map from database ID to its safetime since epoch in micro-seconds to use "
              + "during unplanned failover")
  @Required
  public Map<String, Long> namespaceIdSafetimeEpochUsMap;
}
