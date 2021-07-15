// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import java.util.UUID;

import play.data.validation.Constraints;

public class AlertRouteFormData {
  @Constraints.Required() public UUID definitionUUID;

  @Constraints.Required() public UUID receiverUUID;
}
