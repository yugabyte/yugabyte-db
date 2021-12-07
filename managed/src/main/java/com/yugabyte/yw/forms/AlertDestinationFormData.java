// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.forms;

import java.util.List;
import java.util.UUID;
import play.data.validation.Constraints;

public class AlertDestinationFormData {
  @Constraints.Required() public String name;

  @Constraints.Required() public Boolean defaultDestination;

  @Constraints.Required() public List<UUID> channels;
}
