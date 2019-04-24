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
import com.yugabyte.yw.common.CallHomeManager;
import com.yugabyte.yw.common.CloudQueryHelper;
import com.yugabyte.yw.common.ReleaseManager;
import com.yugabyte.yw.forms.CustomerRegisterFormData;
import com.yugabyte.yw.metrics.MetricQueryHelper;
import com.yugabyte.yw.forms.MetricQueryParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerConfig;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.data.Form;
import play.data.FormFactory;
import play.libs.Json;
import play.mvc.Result;


public class CustomerController extends AuthenticatedController {

  public static final Logger LOG = LoggerFactory.getLogger(CustomerController.class);

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

    ObjectNode responseJson = (ObjectNode)Json.toJson(customer);
    CustomerConfig config = CustomerConfig.getAlertConfig(customerUUID);
    if (config != null) {
      responseJson.set("alertingData", config.data);
    }
    responseJson.put("callhomeLevel", CustomerConfig.getOrCreateCallhomeLevel(customerUUID).toString());

    responseJson.put("features", customer.getFeatures());

    return ok(responseJson);
  }

  public Result update(UUID customerUUID) {
    ObjectNode responseJson = Json.newObject();
    ObjectNode errorJson = Json.newObject();

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

    boolean hasPassword = formData.get().password != null && !formData.get().password.isEmpty();
    boolean hasConfirmPassword = formData.get().confirmPassword != null &&
            !formData.get().confirmPassword.isEmpty();
    if (hasPassword && hasConfirmPassword) {
      if (!formData.get().password.equals(formData.get().confirmPassword)) {
        String errorMsg = "Both passwords must match!";
        errorJson.put("password", errorMsg);
        errorJson.put("confirmPassword", errorMsg);
        responseJson.set("error", errorJson);
        return badRequest(responseJson);
      }
      // Had password info that matched, should update user password!
      customer.setPassword(formData.get().password);
    } else if (hasPassword && !hasConfirmPassword) {
      String errorMsg = "Please re-enter password";
      errorJson.put("confirmPassword", errorMsg);
      responseJson.set("error", errorJson);
      return badRequest(responseJson);
    } else if (!hasPassword && hasConfirmPassword) {
      String errorMsg = "Please enter password";
      errorJson.put("password", errorMsg);
      responseJson.set("error", errorJson);
      return badRequest(responseJson);
    }

    CustomerConfig config = CustomerConfig.getAlertConfig(customerUUID);
    if (config == null && formData.get().alertingData != null) {
      config = CustomerConfig.createAlertConfig(
              customerUUID, Json.toJson(formData.get().alertingData));
    } else if (config != null) {
      config.data = Json.toJson(formData.get().alertingData);
      config.update();
    }

    String features = formData.get().features;
    if (features != null) {
      customer.upsertFeatures(Json.toJson(features));
    }

    CustomerConfig.upsertCallhomeConfig(customerUUID, formData.get().callhomeLevel);

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

    // Given we have a limitation on not being able to rename the pod labels in kuberentes cadvisor metrics,
    // we try to see if the metric being queried is for container or not, and use pod_name vs exported_instance
    // accordingly. Expect for container metrics, all the metrics would with node_prefix and exported_instance.
    boolean hasContainerMetric = formData.get().metrics.stream().anyMatch(s -> s.startsWith("container"));
    String universeFilterLabel = hasContainerMetric ? "namespace" : "node_prefix";
    String nodeFilterLabel = hasContainerMetric ? "pod_name" : "exported_instance";

    ObjectNode filterJson = Json.newObject();
    if (!params.containsKey("nodePrefix")) {
      String universePrefixes = customer.getUniverses().stream()
        .map((universe -> universe.getUniverseDetails().nodePrefix)).collect(Collectors.joining("|"));
      filterJson.put(universeFilterLabel, String.join("|", universePrefixes));
    } else {
      filterJson.put(universeFilterLabel, params.remove("nodePrefix"));
      if (params.containsKey("nodeName")) {
        filterJson.put(nodeFilterLabel, params.remove("nodeName"));
      }
    }
    if (params.containsKey("tableName")) {
      filterJson.put("table_name", params.remove("tableName"));
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

  public Result getHostInfo(UUID customerUUID) {
    Customer customer = Customer.get(customerUUID);
    if (customer == null) {
      return ApiResponse.error(BAD_REQUEST, "Invalid Customer UUID: " + customerUUID);
    }
    ObjectNode hostInfo = Json.newObject();
    hostInfo.put(Common.CloudType.aws.name(), cloudQueryHelper.currentHostInfo(
        Common.CloudType.aws, ImmutableList.of("instance-id", "vpc-id", "privateIp", "region")));
    hostInfo.put(Common.CloudType.gcp.name(), cloudQueryHelper.currentHostInfo(
        Common.CloudType.gcp, null));

    return ApiResponse.success(hostInfo);
  }
}
