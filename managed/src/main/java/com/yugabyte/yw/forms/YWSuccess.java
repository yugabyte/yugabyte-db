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
import play.libs.Json;
import play.mvc.Result;

import static play.mvc.Results.ok;

@JsonInclude(JsonInclude.Include.NON_NULL)
public class YWSuccess {
  public final boolean success;
  public final String message;

  YWSuccess() {
    this(true, null);
  }

  YWSuccess(boolean success, String message) {
    this.success = success;
    this.message = message;
  }

  public static Result asResult() {
    return ok(Json.toJson(new YWSuccess()));
  }

  public static Result asResult(String message) {
    return ok(Json.toJson(new YWSuccess(true, message)));
  }
}
