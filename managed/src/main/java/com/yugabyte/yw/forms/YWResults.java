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
import com.fasterxml.jackson.databind.JsonNode;
import org.apache.commons.lang3.StringUtils;
import play.libs.Json;
import play.mvc.Result;

import java.util.List;
import java.util.UUID;

import static play.mvc.Results.ok;

public class YWResults {

  @JsonInclude(JsonInclude.Include.NON_NULL)
  public static class YWStructuredError {
    public final boolean success = false;

    public final JsonNode  error;

    public YWStructuredError(JsonNode err) {
      error = err;
    }
  }


  static class OkResult {
    public Result asResult() {
      return ok(Json.toJson(this));
    }
  }

  @JsonInclude(JsonInclude.Include.NON_NULL)
  public static class YWSuccess extends OkResult {
    public final boolean success;
    public final String message;

    YWSuccess() {
      this(true, null);
    }

    YWSuccess(boolean success, String message) {
      this.success = success;
      this.message = message;
    }

    public static Result empty() {
      return new YWSuccess().asResult();
    }

    public static Result withMessage(String message) {
      return new YWSuccess(true, message).asResult();
    }
  }

  public static class YWTask extends OkResult {
    public final UUID taskUUID;

    public YWTask(UUID taskUUID) {
      this.taskUUID = taskUUID;
    }
  }

  public static class YWTasks extends OkResult {
    public final List<UUID> taskUUID;

    public YWTasks(List<UUID> taskUUID) {
      this.taskUUID = taskUUID;
    }
  }

}
