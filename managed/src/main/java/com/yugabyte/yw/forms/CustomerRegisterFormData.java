// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.forms;

import com.yugabyte.yw.common.alerts.SmtpData;
import io.swagger.annotations.ApiModelProperty;
import java.util.Map;
import play.data.validation.Constraints;

/** This class will be used by the API and UI Form Elements to validate constraints are met */
public class CustomerRegisterFormData {
  @Constraints.Required()
  @Constraints.MaxLength(15)
  private String code;

  @Constraints.Required() @Constraints.Email private String email;

  @Constraints.Required() private String password;

  @ApiModelProperty(value = "UI_ONLY", hidden = true)
  private String confirmPassword;

  @Constraints.Required()
  @Constraints.MinLength(3)
  private String name;

  @ApiModelProperty(value = "UI_ONLY", hidden = true)
  private Map features;

  public String getCode() {
    return code;
  }

  public void setCode(String code) {
    this.code = code;
  }

  public String getEmail() {
    return email;
  }

  public void setEmail(String email) {
    this.email = email;
  }

  public String getPassword() {
    return password;
  }

  public void setPassword(String password) {
    this.password = password;
  }

  public String getConfirmPassword() {
    return confirmPassword;
  }

  public void setConfirmPassword(String confirmPassword) {
    this.confirmPassword = confirmPassword;
  }

  public String getName() {
    return name;
  }

  public void setName(String name) {
    this.name = name;
  }

  public Map getFeatures() {
    return features;
  }

  public void setFeatures(Map features) {
    this.features = features;
  }

  public AlertingData getAlertingData() {
    return alertingData;
  }

  public void setAlertingData(AlertingData alertingData) {
    this.alertingData = alertingData;
  }

  public SmtpData getSmtpData() {
    return smtpData;
  }

  public void setSmtpData(SmtpData smtpData) {
    this.smtpData = smtpData;
  }

  public String getCallhomeLevel() {
    return callhomeLevel;
  }

  public void setCallhomeLevel(String callhomeLevel) {
    this.callhomeLevel = callhomeLevel;
  }

  // TODO: Remove usages in UI and purge the field
  @ApiModelProperty(value = "UNUSED", hidden = true)
  public AlertingData alertingData;

  // TODO: Remove usages in UI and purge the field
  @ApiModelProperty(value = "UNUSED", hidden = true)
  public SmtpData smtpData;

  @Constraints.Pattern(
      message = "Must be one of NONE, LOW, MEDIUM, HIGH",
      value = "\\b(?:NONE|LOW|MEDIUM|HIGH)\\b")
  // TODO: Remove usages in UI and purge the field
  @ApiModelProperty(value = "UNUSED", hidden = true)
  public String callhomeLevel = "MEDIUM";
}
