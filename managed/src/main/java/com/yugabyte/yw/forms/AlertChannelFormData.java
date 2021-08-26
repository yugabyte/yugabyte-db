// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import com.yugabyte.yw.common.alerts.AlertChannelParams;
import java.util.UUID;
import play.data.validation.Constraints;

public class AlertChannelFormData {

  public UUID alertChannelUUID;

  @Constraints.Required() public String name;

  @Constraints.Required() public AlertChannelParams params;
}
