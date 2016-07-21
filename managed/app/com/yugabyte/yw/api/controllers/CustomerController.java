// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.api.controllers;

import java.util.UUID;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.inject.Inject;
import com.yugabyte.yw.api.forms.CustomerRegisterFormData;
import com.yugabyte.yw.api.models.Customer;
import com.yugabyte.yw.common.controllers.AuthenticatedController;

import play.data.Form;
import play.data.FormFactory;
import play.libs.Json;
import play.mvc.Result;


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

		return ok(Json.toJson(customer));
	}
}
