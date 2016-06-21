// Copyright (c) Yugabyte, Inc.

package controllers;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.inject.Inject;
import forms.yb.CustomerLoginFormData;
import forms.yb.CustomerRegisterFormData;
import models.yb.Customer;
import play.data.Form;
import play.data.FormFactory;
import play.libs.Json;
import play.mvc.*;
import security.TokenAuthenticator;

public class SessionController extends Controller {

  @Inject
  FormFactory formFactory;

  public static final String AUTH_TOKEN = "authToken";

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
		response().setCookie(Http.Cookie.builder(AUTH_TOKEN, authToken).withSecure(ctx().request().secure()).build());
		return ok(authTokenJson);
	}

  public Result register() {
		Form<CustomerRegisterFormData> formData = formFactory.form(CustomerRegisterFormData.class).bindFromRequest();
	  ObjectNode responseJson = Json.newObject();

		if (formData.hasErrors()) {
			responseJson.set("error", formData.errorsAsJson());
			return badRequest(responseJson);
		}

	  CustomerRegisterFormData data = formData.get();
		Customer cust = Customer.create(data.name, data.email, data.password);

		if (cust == null) {
			responseJson.put("error", "Unable to register the customer");
			return internalServerError(responseJson);
		}

	  String authToken = cust.createAuthToken();
	  ObjectNode authTokenJson = Json.newObject();
	  authTokenJson.put(AUTH_TOKEN, authToken);
	  response().setCookie(Http.Cookie.builder(AUTH_TOKEN, authToken).withSecure(ctx().request().secure()).build());
	  return ok(authTokenJson);
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
