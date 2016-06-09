// Copyright (c) Yugabyte, Inc.

package controllers;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.inject.Inject;
import forms.LoginFormData;
import forms.RegisterFormData;
import models.Customer;
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
		Form<LoginFormData> formData = formFactory.form(LoginFormData.class).bindFromRequest();

		if (formData.hasErrors()) {
			return badRequest(formData.errorsAsJson());
		}

		LoginFormData data = formData.get();
		Customer cust = Customer.authWithPassword(data.email.toLowerCase(), data.password);

		if (cust == null) {
			return unauthorized();
		}

		String authToken = cust.createAuthToken();
		ObjectNode authTokenJson = Json.newObject();
		authTokenJson.put(AUTH_TOKEN, authToken);
		response().setCookie(Http.Cookie.builder(AUTH_TOKEN, authToken).withSecure(ctx().request().secure()).build());
		return ok(authTokenJson);
	}

  public Result register() {
		Form<RegisterFormData> formData = formFactory.form(RegisterFormData.class).bindFromRequest();

		if (formData.hasErrors()) {
			return badRequest(formData.errorsAsJson());
		}

		RegisterFormData data = formData.get();
		Customer cust = Customer.create(data.name, data.email, data.password);

		if (cust == null) {
			return internalServerError();
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
