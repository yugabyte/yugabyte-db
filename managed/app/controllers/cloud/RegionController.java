// Copyright (c) Yugabyte, Inc.

package controllers.cloud;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.inject.Inject;
import controllers.AuthenticatedController;
import forms.cloud.RegionFormData;
import models.cloud.Provider;
import models.cloud.Region;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.data.Form;
import play.data.FormFactory;
import play.libs.Json;
import play.mvc.Result;

import java.util.List;
import java.util.UUID;

public class RegionController extends AuthenticatedController {
	@Inject
	FormFactory formFactory;
	public static final Logger LOG = LoggerFactory.getLogger(RegionController.class);

	/**
	 * GET endpoint for listing regions
	 * @return JSON response with region's
	 */
	public Result list(UUID providerUUID) {
		List<Region> regionList = null;
		ObjectNode responseJson = Json.newObject();

		boolean multiAZ = false;
		if (request().getQueryString("multiAZ") != null) {
			multiAZ = request().getQueryString("multiAZ").equals("true") ? true : false;
		}

		try {
			if (multiAZ) {
				regionList = Region.find.where().eq("provider_uuid", providerUUID).eq("multi_az_capable", true).findList();
			} else {
				regionList = Region.find.where().eq("provider_uuid", providerUUID).findList();
			}
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
			Region p = Region.create(provider, formData.get().code, formData.get().name, true);
			return ok(Json.toJson(p));
		} catch (Exception e) {
			// TODO: Handle exception and print user friendly message
			responseJson.put("error", e.getMessage());
			return internalServerError(responseJson);
		}
	}
}
