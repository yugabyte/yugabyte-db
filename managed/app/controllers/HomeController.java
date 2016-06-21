// Copyright (c) Yugabyte, Inc.

package controllers;

import com.google.inject.Inject;
import controllers.yb.CustomerController;
import forms.yb.CustomerLoginFormData;
import forms.yb.CustomerRegisterFormData;
import play.data.Form;
import play.data.FormFactory;
import play.mvc.Controller;
import play.mvc.Result;
import play.mvc.With;
import security.TokenAuthenticator;
import views.html.*;

import static security.TokenAuthenticator.COOKIE_AUTH_TOKEN;

public class HomeController extends Controller {

	@Inject
	FormFactory formFactory;

	public Result login() {
		Form<CustomerLoginFormData> formData = formFactory.form(CustomerLoginFormData.class);
		return ok(loginForm.render(formData));
	}

	public Result register() {
		Form<CustomerRegisterFormData> formData = formFactory.form(CustomerRegisterFormData.class);
		return ok(registerForm.render(formData));
	}
}
