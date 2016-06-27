// Copyright (c) YugaByte, Inc.

package controllers;

import com.google.inject.Inject;
import forms.yb.CreateInstanceFormData;
import forms.yb.CustomerRegisterFormData;
import models.yb.Customer;
import models.yb.Instance;
import play.data.Form;
import play.data.FormFactory;
import play.mvc.Result;
import views.html.*;

import java.util.UUID;

public class DashboardController extends AuthenticatedController {
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
