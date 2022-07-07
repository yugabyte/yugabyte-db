// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import play.data.validation.Constraints;

/**
 * This class will be used by the API and UI Form Elements to validate constraints for AlertConfig
 * changes at the universe level
 */
public class AlertConfigFormData {
  @Constraints.Required() public Boolean disabled;

  // when enabled is set to false, alerts will be disabled for disablePeriodSecs only
  // if a value is specified
  @Constraints.Min(1)
  public Long disablePeriodSecs;
}
