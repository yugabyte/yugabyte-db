// Copyright (c) Yugabyte, Inc.

package controllers;

import com.google.inject.Inject;
import forms.LoginFormData;
import forms.RegisterFormData;
import play.data.Form;
import play.data.FormFactory;
import play.mvc.Controller;
import play.mvc.Result;
import views.html.*;

public class HomeController extends Controller {

	@Inject
	FormFactory formFactory;

	public Result index() {
		if (ctx().request().cookie(SessionController.AUTH_TOKEN) == null) {
			Form<LoginFormData> formData = formFactory.form(LoginFormData.class);
			return ok(loginForm.render(formData));
		}
		return ok(dashboard.render());
	}

	public Result login() {
		Form<LoginFormData> formData = formFactory.form(LoginFormData.class);
		return ok(loginForm.render(formData));
	}

	public Result register() {
		Form<RegisterFormData> formData = formFactory.form(RegisterFormData.class);
		return ok(registerForm.render(formData));
	}
}
