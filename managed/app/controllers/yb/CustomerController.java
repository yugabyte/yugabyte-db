// Copyright (c) Yugabyte, Inc.

package controllers.yb;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.inject.Inject;
import controllers.AuthenticatedController;
import forms.yb.CustomerLoginFormData;
import forms.yb.CustomerRegisterFormData;
import models.yb.Customer;
import play.data.Form;
import play.data.FormFactory;
import play.libs.Json;
import play.mvc.*;

import java.util.UUID;


public class CustomerController extends AuthenticatedController {

  @Inject
  FormFactory formFactory;

	public Result index(UUID customerUUID) {
		Customer customer = Customer.find.byId(customerUUID);

		if (customer == null) {
			ObjectNode responseJson = Json.newObject();
			responseJson.put("error", "Invalid Customer UUID:" + customerUUID);
			return badRequest(responseJson);
		}

		return ok(Json.toJson(customer));
	}

	public Result update(UUID customerUUID) {
		ObjectNode responseJson = Json.newObject();

		Customer customer = Customer.find.byId(customerUUID);
		if (customer == null) {
			responseJson.put("error", "Invalid Customer UUID:" + customerUUID);
			return badRequest(responseJson);
		}

		Form<CustomerRegisterFormData> formData = formFactory.form(CustomerRegisterFormData.class).bindFromRequest();
		if (formData.hasErrors()) {
			responseJson.set("error", formData.errorsAsJson());
			return badRequest(responseJson);
		}


		customer.setPassword(formData.get().password);
		customer.name = formData.get().name;
		customer.update();

		if (customer == null) {
			responseJson.put("error", "Unable to update the customer");
			return internalServerError(responseJson);
		}

		return ok(Json.toJson(customer));
	}
}
