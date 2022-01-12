/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.forms;

import play.data.validation.Constraints;

public class PlatformInstanceFormData {
  @Constraints.Required()
  @Constraints.Pattern(
      message = "Must be prefixed with http:// or https://",
      value = "\\b(?:http://|https://).+(/|\\b)")
  @YugabyteConstraints.ValidURL()
  public String address;

  @Constraints.Required() public boolean is_leader;

  @Constraints.Required() public boolean is_local;

  public String getCleanAddress() {
    return address.replaceAll("/$", "");
  }
}
