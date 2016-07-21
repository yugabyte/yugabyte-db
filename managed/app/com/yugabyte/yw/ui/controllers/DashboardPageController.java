// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.ui.controllers;

import com.google.inject.Inject;
import com.yugabyte.yw.api.forms.CreateInstanceFormData;
import com.yugabyte.yw.api.forms.CustomerRegisterFormData;
import com.yugabyte.yw.api.models.Customer;
import com.yugabyte.yw.api.models.Instance;
import com.yugabyte.yw.common.controllers.AuthenticatedController;

import play.data.Form;
import play.data.FormFactory;
import play.mvc.Result;
import com.yugabyte.yw.ui.views.html.*;

import java.util.UUID;

public class DashboardPageController extends AuthenticatedController {
  @Inject
  FormFactory formFactory;
  public Result index() {
    UUID customerUUID = (UUID) ctx().args.get("customer_uuid");
    Integer instanceCount = Instance.find.where().eq("customer_uuid", customerUUID).findList().size();
    // TODO: need to fetch this from a API.
    Integer tableCount = 0;
    return ok(dashboard.render(instanceCount, tableCount));
  }

  public Result createInstance() {
		Form<CreateInstanceFormData> formData = formFactory.form(CreateInstanceFormData.class);
    return ok(createInstance.render(formData));
  }

  public Result instances() {
    return ok(listInstance.render());
  }

  public Result tables() {
    return TODO;
  }

  public Result profile() {
    UUID currentProfileUUID = (UUID) ctx().args.get("customer_uuid");
    if (currentProfileUUID != null) {
      Customer currentProfile = Customer.find.byId(currentProfileUUID);
      CustomerRegisterFormData data = CustomerRegisterFormData.createFromCustomer(currentProfile);
      Form<CustomerRegisterFormData> formData = formFactory.form(CustomerRegisterFormData.class).fill(data);
      return ok(editProfile.render(formData));
    }
    return redirect("/");
  }

  public Result showInstance(UUID instanceUUID) {
    return TODO;
  }
}
