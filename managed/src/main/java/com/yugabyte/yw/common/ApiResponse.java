// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;
import play.mvc.Result;
import play.mvc.Results;

import static play.mvc.Http.Status.OK;

public class ApiResponse {
  public static final Logger LOG = LoggerFactory.getLogger(ApiResponse.class);

  /**
   * @deprecated Instead throw {@link YWServiceException}
   */
  @Deprecated
  public static Result error(int status, Object message) {
    LOG.error("Hit error " + status + ", message: " + errorJSON(message));
    return Results.status(status, errorJSON(message));
  }

  public static Result success(Object message) {
    return Results.status(OK, Json.toJson(message));
  }

  /**
   * @deprecated Instead throw {@link YWServiceException}
   */
  public static JsonNode errorJSON(Object message) {
    ObjectNode jsonMsg = Json.newObject();

    if (message instanceof JsonNode)
      jsonMsg.set("error", (JsonNode) message);
    else
      jsonMsg.put("error", (String) message);

    return jsonMsg;
  }
}
