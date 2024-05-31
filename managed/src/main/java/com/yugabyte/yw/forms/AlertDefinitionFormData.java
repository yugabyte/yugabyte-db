/*
 * Copyright 2020 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.forms;

import com.yugabyte.yw.common.AlertTemplate;
import java.util.UUID;
import play.data.validation.Constraints;

/** This class will be used by the API and UI Form Elements to validate constraints are met. */
public class AlertDefinitionFormData {

  public UUID alertDefinitionUUID;

  public AlertTemplate template;

  @Constraints.Required() public double value;

  @Constraints.Required() public String name;

  @Constraints.Required() public boolean active;
}
