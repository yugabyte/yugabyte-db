// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.ui.controllers;

import java.util.UUID;

import com.google.inject.Inject;
import com.yugabyte.yw.common.ApiResponse;
import com.yugabyte.yw.forms.CreateUniverseFormData;
import com.yugabyte.yw.forms.CustomerRegisterFormData;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.ui.views.html.*;

import play.data.Form;
import play.data.FormFactory;
import play.mvc.Result;

public class DashboardPageController extends AuthenticatedController {
  @Inject
  FormFactory formFactory;
  public Result index() {
    UUID customerUUID = (UUID) ctx().args.get("customer_uuid");
    // Verify the customer with this universe is present.
    Customer customer = Customer.find.byId(customerUUID);
    if (customer == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Customer UUID: " + customerUUID);
    }
    Integer universeCount =
        (customer.getUniverseUUIDs() == null) ? 0 : customer.getUniverseUUIDs().size();
    // TODO: need to fetch this from a API.
    Integer tableCount = 0;
    return ok(dashboard.render(universeCount, tableCount));
  }

  public Result createInstance() {
		Form<CreateUniverseFormData> formData = formFactory.form(CreateUniverseFormData.class);
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
