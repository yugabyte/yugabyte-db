package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonUnwrapped;
import com.yugabyte.yw.models.PACollector;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import lombok.Data;
import lombok.experimental.Accessors;

@Data
@Accessors(chain = true)
@ApiModel("PA Collector Details Model")
public class PACollectorExt {
  @JsonUnwrapped PACollector paCollector;

  @ApiModelProperty("In Use Status")
  InUseStatus inUseStatus;

  public enum InUseStatus {
    IN_USE,
    NOT_IN_USE,
    ERROR
  }
}
