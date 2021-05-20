// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.forms;

import play.data.validation.Constraints;

import java.util.Date;
import java.util.UUID;

/** This class will be used by the API and UI Form Elements to validate constraints are met. */
public class AlertFormData {
  @Constraints.Required() public String errCode;

  @Constraints.Required() public String type;

  @Constraints.Required() public String message;
}
