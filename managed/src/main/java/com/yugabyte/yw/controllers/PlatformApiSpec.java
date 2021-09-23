/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.controllers;

import play.mvc.Controller;
import play.mvc.Result;

public class PlatformApiSpec extends Controller {

  public Result getStaticSwaggerSpec() {
    return ok(getClass().getResourceAsStream("/swagger.json"));
  }
}
