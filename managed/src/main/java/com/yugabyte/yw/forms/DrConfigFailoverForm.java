package com.yugabyte.yw.forms;

import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.Map;
import lombok.ToString;
import play.data.validation.Constraints.Required;

@ApiModel(description = "drConfig failover form")
@ToString
public class DrConfigFailoverForm {

  @ApiModelProperty(
      value =
          "A map from database ID to its safetime since epoch in micro-seconds to use "
              + "during unplanned failover")
  public Map<String, Long> namespaceIdSafetimeEpochUsMap;

  @Required
  @ApiModelProperty(value = "Status", allowableValues = "PLANNED, UNPLANNED")
  public Type type;

  public enum Type {
    // Planned failover with zero RPO.
    PLANNED,
    // Unplanned failover with some RPO when the primary universe is down.
    UNPLANNED;

    @Override
    public String toString() {
      return name().toLowerCase();
    }
  }
}
