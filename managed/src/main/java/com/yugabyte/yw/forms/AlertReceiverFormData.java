// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import java.util.UUID;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.models.AlertReceiver.TargetType;

import play.data.validation.Constraints;

public class AlertReceiverFormData {

  public UUID alertReceiverUUID;

  @Constraints.Required() public TargetType targetType;

  @Constraints.Required() public JsonNode params;
}
