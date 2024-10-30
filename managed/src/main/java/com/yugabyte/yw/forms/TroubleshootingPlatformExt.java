package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonUnwrapped;
import com.yugabyte.yw.models.TroubleshootingPlatform;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import lombok.Data;
import lombok.experimental.Accessors;

@Data
@Accessors(chain = true)
@ApiModel("Troubleshooting Platform Details Model")
public class TroubleshootingPlatformExt {
  @JsonUnwrapped TroubleshootingPlatform troubleshootingPlatform;

  @ApiModelProperty("In Use Status")
  InUseStatus inUseStatus;

  public enum InUseStatus {
    IN_USE,
    NOT_IN_USE,
    ERROR
  }
}
