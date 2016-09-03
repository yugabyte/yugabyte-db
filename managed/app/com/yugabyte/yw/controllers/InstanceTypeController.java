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
import com.yugabyte.yw.ui.controllers.AuthenticatedController;

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
}
