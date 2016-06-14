// Copyright (c) Yugabyte, Inc.

package controllers.cloud;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.inject.Inject;
import controllers.AuthenticatedController;
import forms.cloud.AvailabilityZoneFormData;
import models.cloud.AvailabilityZone;
import models.cloud.Region;
import play.data.Form;
import play.data.FormFactory;
import play.libs.Json;
import play.mvc.Result;

import java.util.List;
import java.util.UUID;

public class AvailabilityZoneController extends AuthenticatedController {
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
			responseJson.put("error", "Invalid Region/Provider UUID");
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
			responseJson.put("error", "Invalid Region/Provider UUID");
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
