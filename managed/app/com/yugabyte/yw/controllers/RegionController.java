// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers;

import java.util.List;
import java.util.UUID;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.inject.Inject;
import com.yugabyte.yw.forms.RegionFormData;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.ui.controllers.AuthenticatedController;

import play.data.Form;
import play.data.FormFactory;
import play.libs.Json;
import play.mvc.Result;

public class RegionController extends AuthenticatedController {
  @Inject
  FormFactory formFactory;
  public static final Logger LOG = LoggerFactory.getLogger(RegionController.class);
  // This constant defines the minimum # of PlacementAZ we need to tag a region as Multi-PlacementAZ complaint
  protected static final int MULTI_AZ_MIN_ZONE_COUNT = 3;

  /**
   * GET endpoint for listing regions
   * @return JSON response with region's
   */
  public Result list(UUID providerUUID) {
    List<Region> regionList = null;
    ObjectNode responseJson = Json.newObject();

    boolean multiAZ = false;
    if (request().getQueryString("isMultiAZ") != null) {
      multiAZ = request().getQueryString("isMultiAZ").equals("true");
    }

    try {
      int azCountNeeded = multiAZ ? MULTI_AZ_MIN_ZONE_COUNT : 1;
      regionList = Region.fetchRegionsWithZoneCount(providerUUID, azCountNeeded);
    } catch (Exception e) {
      responseJson.put("error", e.getMessage());
      return internalServerError(responseJson);
    }
    return ok(Json.toJson(regionList));
  }

  /**
   * POST endpoint for creating new region
   * @return JSON response of newly created region
   */
  public Result create(UUID providerUUID) {
    Form<RegionFormData> formData = formFactory.form(RegionFormData.class).bindFromRequest();
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
      Region p = Region.create(provider, formData.get().code, formData.get().name);
      return ok(Json.toJson(p));
    } catch (Exception e) {
      // TODO: Handle exception and print user friendly message
      responseJson.put("error", e.getMessage());
      return internalServerError(responseJson);
    }
  }
}
