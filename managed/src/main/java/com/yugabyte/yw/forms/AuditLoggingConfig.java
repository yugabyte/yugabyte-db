// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.yugabyte.yw.common.BeanValidator;
import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.common.YbaApi.YbaApiVisibility;
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

  @ApiModelProperty(
      value =
          "WARNING: This is a preview API that could change. "
              + "Audit log file name prefix. Defaults to \"\". For example, setting this to"
              + " \"yb-platform-\" will generate audit log files as \"yb-platform-audit.log\""
              + " instead of \"audit.log\".",
      example = "yb-platform-",
      required = false)
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.21.0.0")
  private String fileNamePrefix;

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
