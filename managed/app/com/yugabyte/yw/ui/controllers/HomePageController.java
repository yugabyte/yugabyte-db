// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.ui.controllers;

import static com.yugabyte.yw.ui.controllers.TokenAuthenticator.COOKIE_AUTH_TOKEN;

import com.google.inject.Inject;
import com.yugabyte.yw.controllers.CustomerController;
import com.yugabyte.yw.forms.CustomerLoginFormData;
import com.yugabyte.yw.forms.CustomerRegisterFormData;

import play.data.Form;
import play.data.FormFactory;
import play.mvc.Controller;
import play.mvc.Result;
import play.mvc.With;
import com.yugabyte.yw.ui.views.html.*;

public class HomePageController extends Controller {

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
