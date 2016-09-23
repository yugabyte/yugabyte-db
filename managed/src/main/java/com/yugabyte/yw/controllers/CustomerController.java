// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers;

import java.util.UUID;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.inject.Inject;
import com.yugabyte.yw.common.ApiResponse;
import com.yugabyte.yw.forms.CustomerRegisterFormData;
import com.yugabyte.yw.models.Customer;

import play.data.Form;
import play.data.FormFactory;
import play.libs.Json;
import play.mvc.Result;


public class CustomerController extends AuthenticatedController {

  @Inject
  FormFactory formFactory;

  public Result index(UUID customerUUID) {
    Customer customer = Customer.get(customerUUID);

    if (customer == null) {
      ObjectNode responseJson = Json.newObject();
      responseJson.put("error", "Invalid Customer UUID:" + customerUUID);
      return badRequest(responseJson);
    }

    return ok(Json.toJson(customer));
  }

  public Result update(UUID customerUUID) {
    ObjectNode responseJson = Json.newObject();

    Customer customer = Customer.get(customerUUID);
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

  public Result delete(UUID customerUUID) {
    Customer customer = Customer.get(customerUUID);

    if (customer == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Customer UUID:" + customerUUID);
    }

    if (customer.delete()) {
      ObjectNode responseJson = Json.newObject();
      responseJson.put("success", true);
      return ApiResponse.success(responseJson);
    } else {
      return ApiResponse.error(INTERNAL_SERVER_ERROR, "Unable to delete Customer UUID: " + customerUUID);
    }
  }
}
