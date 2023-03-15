// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.yugabyte.yw.common.BeanValidator;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.text.SimpleDateFormat;
import javax.validation.constraints.Min;
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

  public void validate(BeanValidator validator) {
    try {
      new SimpleDateFormat(rolloverPattern);
    } catch (Exception e) {
      validator.error().forField("rolloverPattern", "Incorrect pattern").throwError();
    }
  }
}
