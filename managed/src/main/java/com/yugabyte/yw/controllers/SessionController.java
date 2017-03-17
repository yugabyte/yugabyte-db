// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.inject.Inject;
import com.yugabyte.yw.common.ApiResponse;
import com.yugabyte.yw.forms.CustomerLoginFormData;
import com.yugabyte.yw.forms.CustomerRegisterFormData;
import com.yugabyte.yw.models.Customer;

import play.Configuration;
import play.data.Form;
import play.data.FormFactory;
import play.libs.Json;
import play.mvc.*;

import javax.persistence.PersistenceException;

public class SessionController extends Controller {

  @Inject
  FormFactory formFactory;

  @Inject
  Configuration appConfig;

  public static final String AUTH_TOKEN = "authToken";
  public static final String CUSTOMER_UUID = "customerUUID";

  public Result login() {
    Form<CustomerLoginFormData> formData = formFactory.form(CustomerLoginFormData.class).bindFromRequest();
    ObjectNode responseJson = Json.newObject();

    if (formData.hasErrors()) {
      responseJson.set("error", formData.errorsAsJson());
      return badRequest(responseJson);
    }

    CustomerLoginFormData data = formData.get();
    Customer cust = Customer.authWithPassword(data.email.toLowerCase(), data.password);

    if (cust == null) {
      responseJson.put("error", "Invalid Customer Credentials");
      return unauthorized(responseJson);
    }

    String authToken = cust.createAuthToken();
    ObjectNode authTokenJson = Json.newObject();
    authTokenJson.put(AUTH_TOKEN, authToken);
    authTokenJson.put(CUSTOMER_UUID, cust.uuid.toString());
    response().setCookie(Http.Cookie.builder(AUTH_TOKEN, authToken).withSecure(ctx().request().secure()).build());
    return ok(authTokenJson);
  }

  public Result register() {
    Form<CustomerRegisterFormData> formData = formFactory.form(CustomerRegisterFormData.class).bindFromRequest();

    if (formData.hasErrors()) {
      return ApiResponse.error(BAD_REQUEST, formData.errorsAsJson());
    }
    boolean multiTenant = appConfig.getBoolean("yb.multiTenant", false);
    int customerCount = Customer.find.all().size();
    if (!multiTenant && customerCount >= 1) {
      return ApiResponse.error(BAD_REQUEST, "Cannot register multiple accounts in Single tenancy.");
    }

    CustomerRegisterFormData data = formData.get();
    try {
      Customer cust = Customer.create(data.name, data.email, data.password);
      if (cust == null) {
        return ApiResponse.error(INTERNAL_SERVER_ERROR, "Unable to register the customer");
      }

      String authToken = cust.createAuthToken();
      ObjectNode authTokenJson = Json.newObject();
      authTokenJson.put(AUTH_TOKEN, authToken);
      authTokenJson.put(CUSTOMER_UUID, cust.uuid.toString());
      response().setCookie(Http.Cookie.builder(AUTH_TOKEN, authToken).withSecure(ctx().request().secure()).build());
      return ok(authTokenJson);
    } catch (PersistenceException pe) {
      return ApiResponse.error(INTERNAL_SERVER_ERROR, "Customer already registered.");
    }
  }

  @With(TokenAuthenticator.class)
  public Result logout() {
    response().discardCookie(AUTH_TOKEN);
    Customer cust = (Customer) Http.Context.current().args.get("customer");
    if (cust != null) {
      cust.deleteAuthToken();
    }
    return ok();
  }
}
