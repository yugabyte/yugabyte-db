// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import play.libs.Json;
import play.mvc.Result;
import play.mvc.Results;

import static play.mvc.Http.Status.OK;

public class ApiResponse {
  public static Result error(int status, Object message) {
    return Results.status(status, errorJSON(message));
  }

  public static Result success(Object message) {
    return Results.status(OK, Json.toJson(message));
  }

  public static JsonNode errorJSON(Object message) {
    ObjectNode jsonMsg = Json.newObject();

    if (message instanceof JsonNode)
      jsonMsg.set("error", (JsonNode) message);
    else
      jsonMsg.put("error", (String) message);

    return jsonMsg;
  }
}
