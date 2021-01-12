// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;

import com.yugabyte.yw.common.ApiResponse;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.CustomerConfig;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import play.libs.Json;
import play.mvc.Result;

import java.util.ArrayList;
import java.util.List;
import java.util.UUID;


public class CustomerConfigController extends AuthenticatedController {
  public static final Logger LOG = LoggerFactory.getLogger(CustomerConfigController.class);

  public Result create(UUID customerUUID) {
    ObjectNode formData = (ObjectNode)request().body().asJson();
    ObjectNode errorJson = validateFormData(formData);
    if (errorJson.size() > 0) {
      return ApiResponse.error(BAD_REQUEST, errorJson);
    }

    CustomerConfig customerConfig = CustomerConfig.createWithFormData(customerUUID, formData);
    Audit.createAuditEntry(ctx(), request(), formData);
    return ApiResponse.success(customerConfig);
  }


  public Result delete(UUID customerUUID, UUID configUUID) {
    CustomerConfig customerConfig = CustomerConfig.get(customerUUID, configUUID);
    if (customerConfig == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid configUUID: " + configUUID);
    }
    if (!customerConfig.delete()) {
      return ApiResponse.error(INTERNAL_SERVER_ERROR,
          "Customer Configuration could not be deleted.");
    }
    Audit.createAuditEntry(ctx(), request());
    return ApiResponse.success("configUUID deleted");
  }

  public Result list(UUID customerUUID) {
    return ApiResponse.success(CustomerConfig.getAll(customerUUID));
  }

  private ObjectNode validateFormData(JsonNode formData) {
    ObjectNode errorJson = Json.newObject();
    if (!formData.hasNonNull("name")) {
      errorJson.set("name", Json.newArray().add("This field is required"));
    }
    if (!formData.hasNonNull("type")) {
      errorJson.set("type", Json.newArray().add("This field is required"));
    } else if (!CustomerConfig.ConfigType.isValid(formData.get("type").asText())) {
      errorJson.set("type", Json.newArray().add("Invalid type provided"));
    }

    if (!formData.hasNonNull("data")) {
      errorJson.set("data", Json.newArray().add("This field is required"));
    } else if (!formData.get("data").isObject()) {
      errorJson.set("data", Json.newArray().add("Invalid data provided, expected a object."));
    }
    return errorJson;
  }
}
