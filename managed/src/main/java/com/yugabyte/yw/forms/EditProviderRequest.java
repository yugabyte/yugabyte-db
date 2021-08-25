/*
 * Copyright 2021 Yugabyte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/yugabyte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.forms;

import java.util.HashMap;
import java.util.Map;

public class EditProviderRequest {
  public String hostedZoneId;
  public Map<String, String> config = new HashMap<>();
}
