// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import static play.mvc.Http.Status.BAD_REQUEST;
import java.text.SimpleDateFormat;
import javax.validation.constraints.Min;
import com.yugabyte.yw.common.PlatformServiceException;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import lombok.Data;
import play.data.validation.Constraints.Required;

@Data
@ApiModel(description = "Audit Logging Configuration")
public class AuditLoggingConfig {

  @Required
  @ApiModelProperty(value = "Flag to enable/disable audit logs output to stdout", required = true)
  private boolean outputToStdout;

  @Required
  @ApiModelProperty(value = "Flag to enable/disable audit logs output to file", required = true)
  private boolean outputToFile;

  @ApiModelProperty(value = "Rollover Pattern", example = "yyyy-MM-dd", required = false)
  private String rolloverPattern = "yyyy-MM-dd";

  @Min(value = 0)
  @ApiModelProperty(
      value = "Max number of days up till which logs are kept",
      example = "30",
      required = false)
  private int maxHistory = 30;

  public void setRolloverPattern(String rolloverPattern) {
    try {
      new SimpleDateFormat(rolloverPattern);
    } catch (Exception e) {
      throw new PlatformServiceException(BAD_REQUEST, "Incorrect pattern " + rolloverPattern);
    }
    this.rolloverPattern = rolloverPattern;
  }
}
