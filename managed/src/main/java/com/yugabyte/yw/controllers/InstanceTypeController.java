// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import java.util.List;
import java.util.UUID;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.inject.Inject;
import com.yugabyte.yw.common.ApiResponse;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.InstanceType.InstanceTypeDetails;
import com.yugabyte.yw.models.Provider;

import play.data.Form;
import play.data.FormFactory;
import play.libs.Json;
import play.mvc.Result;

public class InstanceTypeController extends AuthenticatedController {
  @Inject
  FormFactory formFactory;
  public static final Logger LOG = LoggerFactory.getLogger(InstanceTypeController.class);

  /**
   * GET endpoint for listing instance types
   *
   * @param providerUUID, UUID of provider
   * @return JSON response with instance types's
   */
  public Result list(UUID providerUUID) {
    List<InstanceType> instanceTypeList = null;
    Provider provider = Provider.find.byId(providerUUID);

    if (provider == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Provider UUID: " + providerUUID);
    }

    try {
      instanceTypeList = InstanceType.findByProvider(provider);
    } catch (Exception e) {
      return ApiResponse.error(INTERNAL_SERVER_ERROR, e.getMessage());
    }
    return ApiResponse.success(instanceTypeList);
  }

  /**
   * POST endpoint for creating new instance type
   *
   * @param providerUUID, UUID of provider
   * @return JSON response of newly created instance type
   */
  public Result create(UUID providerUUID) {
    Form<InstanceType> formData = formFactory.form(InstanceType.class).bindFromRequest();
    Provider provider = Provider.find.byId(providerUUID);
    ObjectNode responseJson = Json.newObject();

    if (provider == null) {
      responseJson.put("error", "Invalid Provider UUID");
      return badRequest(responseJson);
    }

    if (formData.hasErrors()) {
      responseJson.set("error", formData.errorsAsJson());
      return badRequest(responseJson);
    }

    try {
      InstanceType it = InstanceType.upsert(formData.get().getProviderCode(),
                                            formData.get().getInstanceTypeCode(),
                                            formData.get().numCores,
                                            formData.get().memSizeGB,
                                            formData.get().volumeCount,
                                            formData.get().volumeSizeGB,
                                            formData.get().volumeType,
                                            new InstanceTypeDetails());
      return ok(Json.toJson(it));
    } catch (Exception e) {
      responseJson.put("error", e.getMessage());
      return internalServerError(responseJson);
    }
  }

  /**
   * DELETE endpoint for deleting instance types.
   * @param providerUUID, UUID of provider
   * @param instanceTypeCode, Instance Type code.
   * @return JSON response to denote if the delete was successful or not.
   */
  public Result delete(UUID providerUUID, String instanceTypeCode) {
    Provider provider = Provider.find.byId(providerUUID);

    if (provider == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Provider UUID: " + providerUUID);
    }

    try {
      InstanceType instanceType = InstanceType.get(provider.code, instanceTypeCode);
      if (instanceType == null) {
        return ApiResponse.error(BAD_REQUEST, "Invalid InstanceType Code: " + instanceTypeCode);
      }

      instanceType.setActive(false);
      instanceType.save();
      ObjectNode responseJson = Json.newObject();
      responseJson.put("success", true);
      return ApiResponse.success(responseJson);
    } catch (Exception e) {
      return ApiResponse.error(INTERNAL_SERVER_ERROR, "Unable to delete InstanceType: " + instanceTypeCode);
    }
  }

  /**
   * Info endpoint for getting instance type information.
   * @param providerUUID, UUID of provider.
   * @param instanceTypeCode, Instance type code.
   * @return JSON response with instance type information.
   */
  public Result index(UUID providerUUID, String instanceTypeCode) {
    Provider provider = Provider.find.byId(providerUUID);

    if (provider == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Provider UUID: " + providerUUID);
    }

    InstanceType instanceType = InstanceType.get(provider.code, instanceTypeCode);
    if (instanceType == null) {
      return ApiResponse.error(BAD_REQUEST, "Instance Type not found: " + instanceTypeCode);
    }
    return ApiResponse.success(instanceType);
  }
}
