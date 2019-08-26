package com.yugabyte.yw.controllers;

import com.google.inject.Inject;

import com.yugabyte.yw.common.ApiResponse;
import com.yugabyte.yw.models.Alert;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.forms.AlertFormData;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.mvc.Result;
import play.data.Form;
import play.data.FormFactory;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import play.libs.Json;

import java.util.List;
import java.util.UUID;
import java.util.stream.Collectors;

public class AlertController extends AuthenticatedController {
  public static final Logger LOG = LoggerFactory.getLogger(AlertController.class);

  @Inject
  FormFactory formFactory;

  /**
   * Lists alerts for given customer.
   */
  public Result list(UUID customerUUID) {
    if (Customer.get(customerUUID) == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Customer UUID: " + customerUUID);
    }
    ArrayNode alerts = Json.newArray();
    for (Alert alert: Alert.get(customerUUID)) {
        alerts.add(alert.toJson());
    }
    return ok(alerts);
  }

  /**
   * Adds new alert for given customer.
   */
  public Result insert(UUID customerUUID) {
    if (Customer.get(customerUUID) == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Customer UUID: " + customerUUID);
    }

    Form<AlertFormData> formData = formFactory.form(AlertFormData.class).bindFromRequest();
    if (formData.hasErrors()) {
      return ApiResponse.error(BAD_REQUEST, formData.errorsAsJson());
    }

    AlertFormData data = formData.get();
    Alert.create(customerUUID, data.type, data.message);
    return ok();
  }
}
