// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.forms;

import com.yugabyte.yw.models.Customer;

import play.data.validation.Constraints;


/**
 * This class will be used by the API and UI Form Elements to validate constraints are met
 */
public class CustomerRegisterFormData {
  @Constraints.Required()
  @Constraints.MaxLength(15)
  public String code;

  @Constraints.Required()
  @Constraints.Email
  @Constraints.MinLength(5)
  public String email;

  @Constraints.MinLength(6)
  public String password;

  @Constraints.MinLength(6)
  public String confirmPassword;

  @Constraints.Required()
  @Constraints.MinLength(3)
  public String name;

  static public class AlertingData {
    @Constraints.Email
    @Constraints.MinLength(5)
    public String alertingEmail;

    public boolean sendAlertsToYb = false;

    public long checkIntervalMs = 0;

    public long statusUpdateIntervalMs = 0;
  }

  public AlertingData alertingData;

  @Constraints.Pattern(message="Must be one of NONE, LOW, MEDIUM, HIGH", value="\\b(?:NONE|LOW|MEDIUM|HIGH)\\b")
  public String callhomeLevel = "MEDIUM";
}
