// Copyright (c) Yugabyte, Inc.
package controllers;

import com.google.inject.Inject;
import models.cloud.Provider;
import play.data.Form;
import play.data.FormFactory;
import play.libs.Json;
import play.mvc.Result;

import java.util.List;

public class ProviderController extends AuthenticatedController  {
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

		if (formData.hasErrors()) {
			return badRequest(formData.errorsAsJson());
		}

		try {
			Provider p = Provider.create(formData.get().type);
			return ok(Json.toJson(p));
		} catch (Exception e) {
			// TODO: Handle exception and print user friendly message
			return internalServerError(e.getMessage());
		}
	}
}
