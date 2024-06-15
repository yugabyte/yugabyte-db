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

public class HAConfigFormData {
  @Constraints.Required()
  @Constraints.MinLength(44)
  @Constraints.MaxLength(44)
  public String cluster_key;

  // Default to disable cert validation
  public boolean accept_any_certificate = true;
}
