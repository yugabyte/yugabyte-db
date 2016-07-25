// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers;

import java.util.List;
import java.util.UUID;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.inject.Inject;
import com.yugabyte.yw.forms.AvailabilityZoneFormData;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.ui.controllers.AuthenticatedController;

import play.data.Form;
import play.data.FormFactory;
import play.libs.Json;
import play.mvc.Result;

public class AvailabilityZoneController extends AuthenticatedController {

  public static final Logger LOG = LoggerFactory.getLogger(AvailabilityZoneController.class);

	@Inject
	FormFactory formFactory;

	/**
	 * GET endpoint for listing availability zones
	 * @return JSON response with availability zone's
	 */
	public Result list(UUID providerUUID, UUID regionUUID) {
		Region region = Region.find.where()
				.idEq(regionUUID)
				.eq("provider_uuid", providerUUID)
				.findUnique();

		ObjectNode responseJson = Json.newObject();

		if (region == null) {
		  LOG.warn("PlacementRegion not found, cloud provider: " + providerUUID + ", region: " + regionUUID);
			responseJson.put("error", "Invalid PlacementRegion/Provider UUID");
			return badRequest(responseJson);
		}

		try {
			List<AvailabilityZone>  zoneList = AvailabilityZone.find.where()
					.eq("region", region)
					.findList();
			return ok(Json.toJson(zoneList));
		} catch (Exception e) {
			// TODO: Handle exception and print user friendly message
			responseJson.put("error", e.getMessage());
			return internalServerError(responseJson);
		}
	}

	/**
	 * POST endpoint for creating new region
	 * @return JSON response of newly created region
	 */
	public Result create(UUID providerUUID, UUID regionUUID) {
		Form<AvailabilityZoneFormData> formData = formFactory.form(AvailabilityZoneFormData.class).bindFromRequest();
		Region region = Region.find.where()
				.idEq(regionUUID)
				.eq("provider_uuid", providerUUID)
				.findUnique();

		ObjectNode responseJson = Json.newObject();

		if (region == null) {
			responseJson.put("error", "Invalid PlacementRegion/Provider UUID");
			return badRequest(responseJson);
		}

		if (formData.hasErrors()) {
			responseJson.set("error", formData.errorsAsJson());
			return badRequest(responseJson);
		}

		AvailabilityZoneFormData azData = formData.get();

		try {
			AvailabilityZone az = AvailabilityZone.create(region, azData.code, azData.name, azData.subnet);
			return ok(Json.toJson(az));
		} catch (Exception e) {
			// TODO: Handle exception and print user friendly message
			responseJson.put("error", e.getMessage());
			return internalServerError(responseJson);
		}
	}
}
