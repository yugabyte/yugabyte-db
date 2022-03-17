// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.forms;

import com.yugabyte.yw.common.alerts.SmtpData;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.Map;
import play.data.validation.Constraints;

/** This class will be used by the API and UI Form Elements to validate constraints are met */
@ApiModel(
    value = "CustomerAlertData",
    description =
        "Format of an alert, used by the API and UI to validate data against input constraints")
public class AlertingFormData {
  @Constraints.MaxLength(15)
  @ApiModelProperty(value = "Alert code")
  public String code;

  @ApiModelProperty(value = "Alert email address", example = "test@example.com")
  public String email;

  @ApiModelProperty(value = "Email password", example = "XurenRknsc")
  public String password;

  @ApiModelProperty(value = "Email password confirmation", example = "XurenRknsc")
  public String confirmPassword;

  @ApiModelProperty(value = "Alert name", example = "Test alert")
  public String name;

  @ApiModelProperty(value = "Features")
  public Map features;

  public AlertingData alertingData;

  public SmtpData smtpData;

  @Constraints.Pattern(
      message = "Must be one of NONE, LOW, MEDIUM, HIGH",
      value = "\\b(?:NONE|LOW|MEDIUM|HIGH)\\b")
  public String callhomeLevel = "MEDIUM";
}
