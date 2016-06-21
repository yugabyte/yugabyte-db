// Copyright (c) Yugabyte, Inc.
package controllers.cloud;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.inject.Inject;
import controllers.AuthenticatedController;
import models.cloud.Provider;
import play.data.Form;
import play.data.FormFactory;
import play.libs.Json;
import play.mvc.Result;

import java.util.List;

public class ProviderController extends AuthenticatedController {
	@Inject
	FormFactory formFactory;

	/**
	 * GET endpoint for listing providers
	 * @return JSON response with provider's
	 */
	public Result list() {
		List<Provider> providerList = Provider.find.all();
		return ok(Json.toJson(providerList));
	}

	/**
	 * POST endpoint for creating new providers
	 * @return JSON response of newly created provider
	 */
	public Result create() {
		Form<Provider> formData = formFactory.form(Provider.class).bindFromRequest();
		ObjectNode responseJson = Json.newObject();

		if (formData.hasErrors()) {
			responseJson.set("error", formData.errorsAsJson());
			return badRequest(responseJson);
		}

		try {
			Provider p = Provider.create(formData.get().name);
			return ok(Json.toJson(p));
		} catch (Exception e) {
			// TODO: Handle exception and print user friendly message
			responseJson.put("error", e.getMessage());
			return internalServerError(responseJson);
		}
	}
}
