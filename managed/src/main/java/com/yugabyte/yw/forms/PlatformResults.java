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

import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;
import static play.mvc.Results.ok;

import com.fasterxml.jackson.annotation.JsonInclude;
import com.fasterxml.jackson.annotation.JsonInclude.Include;
import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.annotations.VisibleForTesting;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.RedactingService;
import com.yugabyte.yw.common.RedactingService.RedactionTarget;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.List;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.AllArgsConstructor;
import play.libs.Json;
import play.mvc.Result;
import play.mvc.Results;

public class PlatformResults {

  /**
   * @deprecated Ypu should not be using this method. This is only for legacy code that used raw
   *     json in response body. Try to come up with concrete type for your response instead of just
   *     `JsonNode`
   */
  @Deprecated
  public static Result withRawData(JsonNode rawJson) {
    return Results.ok(rawJson);
  }

  private static Result redactedResult(JsonNode dataObj) {
    return Results.ok(RedactingService.filterSecretFields(dataObj, RedactionTarget.APIS));
  }

  /**
   * This is a replacement for ApiResponse.success
   *
   * @param data - to be serialized to json and returned
   */
  public static Result withData(Object data) {
    JsonNode dataObj = Json.toJson(data);
    return redactedResult(dataObj);
  }

  public static Result withData(Object data, Class<?> view) {
    try {
      JsonNode dataObj =
          Json.parse(Json.mapper().copy().writerWithView(view).writeValueAsString(data));
      return redactedResult(dataObj);
    } catch (JsonProcessingException e) {
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, e.getMessage());
    }
  }

  @ApiModel(description = "Generic error response from the YugabyteDB Anywhere API")
  @AllArgsConstructor
  @JsonInclude(Include.NON_NULL)
  public static class YBPError {
    @ApiModelProperty(value = "Always set to false to indicate failure", example = "false")
    public final boolean success = false;

    @ApiModelProperty(value = "Method for HTTP call that resulted in this error", example = "POST")
    public String httpMethod;

    @ApiModelProperty(
        value = "URI for HTTP request that resulted in this error",
        example = "/customers/8918921-af3782-633de/universe/8173ab-fd2453/create")
    public String requestUri;

    @ApiModelProperty(
        value = "User-visible unstructured error message",
        example = "There was a problem creating the universe")
    public String error;

    @ApiModelProperty(
        value = "User visible structured error message as json object",
        dataType = "Object",
        example = "{ \"foo\" : \"bar\", \"baz\" : [1, 2, 3] }")
    public JsonNode errorJson;

    // for json deserialization
    public YBPError() {}

    public YBPError(String error) {
      this.error = error;
    }

    public YBPError(JsonNode errorJson) {
      this.error = "See structured error message in errorJson field";
      this.errorJson = errorJson;
    }
  }

  // TODO: Replace with new YBPError(jsonNode)
  @JsonInclude(JsonInclude.Include.NON_NULL)
  public static class YBPStructuredError {
    public boolean success = false;

    @ApiModelProperty(
        value = "User visible error message as JSON object",
        dataType = "Object",
        example = "{ \"foo\" : \"bar\", \"baz\" : [1, 2, 3] }",
        required = false)
    public JsonNode error;

    // for json deserialization
    YBPStructuredError() {}

    public YBPStructuredError(JsonNode err) {
      error = err;
    }
  }

  static class OkResult {
    public Result asResult() {
      return ok(Json.toJson(this));
    }
  }

  @JsonInclude(JsonInclude.Include.NON_NULL)
  public static class YBPSuccess extends OkResult {

    @ApiModelProperty(
        value = "API operation status. A value of true indicates the operation was successful.",
        accessMode = ApiModelProperty.AccessMode.READ_ONLY)
    public final boolean success;

    @ApiModelProperty(
        value = "API response message.",
        accessMode = ApiModelProperty.AccessMode.READ_ONLY)
    public final String message;

    YBPSuccess() {
      this(true, null);
    }

    YBPSuccess(boolean success, String message) {
      this.success = success;
      this.message = message;
    }

    public static Result empty() {
      return new YBPSuccess().asResult();
    }

    public static Result withMessage(String message) {
      return new YBPSuccess(true, message).asResult();
    }
  }

  public static class YBPTask extends OkResult {
    @VisibleForTesting
    @ApiModelProperty(value = "Task UUID", accessMode = ApiModelProperty.AccessMode.READ_ONLY)
    public UUID taskUUID;

    @ApiModelProperty(
        value = "UUID of the resource being modified by the task",
        accessMode = ApiModelProperty.AccessMode.READ_ONLY)
    @VisibleForTesting
    public UUID resourceUUID;

    // for json deserialization
    public YBPTask() {}

    public YBPTask(UUID taskUUID) {
      this(taskUUID, null);
    }

    public YBPTask(UUID taskUUID, UUID resourceUUID) {
      this.taskUUID = taskUUID;
      this.resourceUUID = resourceUUID;
    }
  }

  public static class YBPTasks extends OkResult {
    @ApiModelProperty(value = "UI_ONLY", hidden = true)
    public final List<UUID> taskUUID;

    @ApiModelProperty(
        value =
            "List of YBTask objects each containing task uuid and uuid of "
                + "resource (like backup, universe, provider) that the task mutated",
        hidden = true)
    public final List<YBPTask> ybpTaskList;

    public YBPTasks(List<YBPTask> ybpTaskList) {
      this.ybpTaskList = ybpTaskList;
      this.taskUUID =
          ybpTaskList.stream().map(ybTask -> ybTask.taskUUID).collect(Collectors.toList());
    }
  }

  public static class YBPCreateSuccess extends OkResult {
    @ApiModelProperty(
        value = "UUID of the successfully created resource",
        accessMode = ApiModelProperty.AccessMode.READ_ONLY)
    @VisibleForTesting
    public final UUID resourceUUID;

    public YBPCreateSuccess(UUID resourceUUID) {
      this.resourceUUID = resourceUUID;
    }
  }
}
