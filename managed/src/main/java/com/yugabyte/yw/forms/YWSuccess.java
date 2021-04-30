/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonInclude;
import play.api.mvc.Results;
import play.libs.Json;
import play.mvc.Result;

@JsonInclude(JsonInclude.Include.NON_NULL)
public class YWSuccess {
  public final boolean success = true;
  public final String message;

  YWSuccess(String message) {
    this.message = message;
  }

  public static Result asResult() {
    return asResult(null);
  }

  public static Result asResult(String message) {
    return play.mvc.Results.status(200, Json.toJson(new YWSuccess(message)));
  }
}
