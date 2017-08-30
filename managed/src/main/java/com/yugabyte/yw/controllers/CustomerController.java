// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers;

import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableList;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.ApiResponse;
import com.yugabyte.yw.common.CloudQueryHelper;
import com.yugabyte.yw.common.ReleaseManager;
import com.yugabyte.yw.forms.CustomerRegisterFormData;
import com.yugabyte.yw.metrics.MetricQueryHelper;
import com.yugabyte.yw.forms.MetricQueryParams;
import com.yugabyte.yw.models.Customer;

import play.data.Form;
import play.data.FormFactory;
import play.libs.Json;
import play.mvc.Result;


public class CustomerController extends AuthenticatedController {

  @Inject
  FormFactory formFactory;

  @Inject
  MetricQueryHelper metricQueryHelper;

  @Inject
  ReleaseManager releaseManager;

  @Inject
  CloudQueryHelper cloudQueryHelper;

  public Result index(UUID customerUUID) {
    Customer customer = Customer.get(customerUUID);
    if (customer == null) {
      ObjectNode responseJson = Json.newObject();
      responseJson.put("error", "Invalid Customer UUID:" + customerUUID);
      return badRequest(responseJson);
    }

    return ok(Json.toJson(customer));
  }

  public Result update(UUID customerUUID) {
    ObjectNode responseJson = Json.newObject();

    Customer customer = Customer.get(customerUUID);
    if (customer == null) {
      responseJson.put("error", "Invalid Customer UUID:" + customerUUID);
      return badRequest(responseJson);
    }

    Form<CustomerRegisterFormData> formData = formFactory.form(CustomerRegisterFormData.class).bindFromRequest();
    if (formData.hasErrors()) {
      responseJson.set("error", formData.errorsAsJson());
      return badRequest(responseJson);
    }


    customer.setPassword(formData.get().password);
    customer.name = formData.get().name;
    customer.update();

    return ok(Json.toJson(customer));
  }

  public Result delete(UUID customerUUID) {
    Customer customer = Customer.get(customerUUID);

    if (customer == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Customer UUID:" + customerUUID);
    }

    if (customer.delete()) {
      ObjectNode responseJson = Json.newObject();
      responseJson.put("success", true);
      return ApiResponse.success(responseJson);
    } else {
      return ApiResponse.error(INTERNAL_SERVER_ERROR, "Unable to delete Customer UUID: " + customerUUID);
    }
  }

  public Result metrics(UUID customerUUID) {
    Customer customer = Customer.get(customerUUID);

    if (customer == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Customer UUID:" + customerUUID);
    }

    Form<MetricQueryParams> formData = formFactory.form(MetricQueryParams.class).bindFromRequest();

    if (formData.hasErrors()) {
      return ApiResponse.error(BAD_REQUEST, formData.errorsAsJson());
    }
    Map<String, String> params = formData.data();
    ObjectNode filterJson = Json.newObject();
    if (!params.containsKey("nodePrefix")) {
      String universePrefixes = customer.getUniverses().stream()
        .map((universe -> universe.getUniverseDetails().nodePrefix)).collect(Collectors.joining("|"));
      filterJson.put("node_prefix", String.join("|", universePrefixes));
    } else {
      filterJson.put("node_prefix", params.remove("nodePrefix"));
      if (params.containsKey("nodeName")) {
        filterJson.put("exported_instance", params.remove("nodeName"));
      }
    }
    params.put("filters", Json.stringify(filterJson));
    try {
      JsonNode response = metricQueryHelper.query(formData.get().metrics, params);
      if (response.has("error")) {
        return ApiResponse.error(BAD_REQUEST, response.get("error"));
      }
      return ApiResponse.success(response);
    } catch (RuntimeException e) {
      return ApiResponse.error(BAD_REQUEST, e.getMessage());
    }
  }

  public Result releases(UUID customerUUID) {
    Customer customer = Customer.get(customerUUID);

    if (customer == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Customer UUID: " + customerUUID);
    }

    Map<String, String> releases = releaseManager.getReleases();
    return ApiResponse.success(releases.keySet());
  }

  public Result getHostInfo(UUID customerUUID) {
    Customer customer = Customer.get(customerUUID);
    if (customer == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Customer UUID: " + customerUUID);
    }
    // TODO: currently we assume the cloudtype to be AWS for fetching host information.
    JsonNode hostInfo = cloudQueryHelper.currentHostInfo(
        Common.CloudType.aws, ImmutableList.of("instance-id", "vpc-id", "privateIp", "region"));

    return ApiResponse.success(hostInfo);
  }
}
