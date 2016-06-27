// Copyright (c) YugaByte, Inc.

package controllers.yb;

import com.fasterxml.jackson.databind.node.ObjectNode;
import controllers.AuthenticatedController;
import models.yb.Customer;
import models.yb.CustomerTask;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Result;
import views.html.*;

import java.util.Set;
import java.util.UUID;

public class CustomerTaskController extends AuthenticatedController {


	protected static final int TASK_HISTORY_LIMIT = 6;

  public Result list(UUID customerUUID) {
    Customer customer = Customer.find.byId(customerUUID);

    if (customer == null) {
      ObjectNode responseJson = Json.newObject();
      responseJson.put("error", "Invalid Customer UUID: " + customerUUID);
      return badRequest(responseJson);
    }

    Set<CustomerTask> pendingTasks = CustomerTask.find.where()
			.eq("customer_uuid", customer.uuid)
			.orderBy("create_time desc")
			.setMaxRows(TASK_HISTORY_LIMIT)
			.findSet();

		if (request().accepts(Http.MimeTypes.HTML)) {
      return ok(customerTask.render(pendingTasks));
    } else if (request().accepts(Http.MimeTypes.JSON)) {
      return ok(Json.toJson(pendingTasks));
    } else {
      return badRequest();
    }
  }
}
