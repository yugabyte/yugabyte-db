// Copyright (c) YugaByte, Inc.

package controllers.yb;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.inject.Inject;
import controllers.AuthenticatedController;
import forms.yb.CreateInstanceFormData;
import helpers.ApiHelper;
import helpers.ApiResponse;
import models.cloud.AvailabilityZone;
import models.yb.Customer;
import models.yb.Instance;
import models.yb.CustomerTask;
import play.data.Form;
import play.data.FormFactory;
import play.libs.Json;
import play.mvc.Result;

import java.util.*;

public class InstanceController extends AuthenticatedController {

  @Inject
  FormFactory formFactory;

	@Inject
	ApiHelper apiHelper;

  public Result create(UUID customerUUID) {
    Form<CreateInstanceFormData> formData = formFactory.form(CreateInstanceFormData.class).bindFromRequest();

    if (formData.hasErrors()) {
			return ApiResponse.error(BAD_REQUEST, formData.errorsAsJson());
		}

    Customer customer = Customer.find.byId(customerUUID);

    if (customer == null) {
			return ApiResponse.error(BAD_REQUEST, "Invalid Customer UUID: " + customerUUID);
		}

    ObjectNode placementInfo = Json.newObject();
    List<AvailabilityZone> azList = AvailabilityZone.find.select("subnet").where().eq("region_uuid", formData.get().regionUUID).findList();

    if (azList.isEmpty()) {
			return ApiResponse.error(INTERNAL_SERVER_ERROR, "Availability Zone not found for region: " + formData.get().regionUUID);
		}

    ArrayNode subnets = Json.newArray();

    int subnetIndex;
    for (int i = 0; i < formData.get().replicationFactor; i++) {
      // TODO: for now, if we want a single AZ, we would just get a first one.
      subnetIndex = formData.get().multiAZ ? i % azList.size() : 0;
      subnets.add(azList.get(subnetIndex).subnet);
    }

    placementInfo.put("regionUUID", formData.get().regionUUID.toString());
    placementInfo.put("replicationFactor", formData.get().replicationFactor);
    placementInfo.put("multiAZ", formData.get().multiAZ);
    placementInfo.set("subnets", subnets);

    try {
      Instance instance = Instance.create(customer, formData.get().name, placementInfo);

			JsonNode commissionerTaskInfo = submitCommissionerTask(instance);
			if (commissionerTaskInfo.has("error")) {
				return ApiResponse.error(INTERNAL_SERVER_ERROR, commissionerTaskInfo);
			}
      UUID commissionerTaskId = UUID.fromString(commissionerTaskInfo.get("taskUUID").asText());
			instance.addTask(commissionerTaskId, CustomerTask.TaskType.Create);
      return ApiResponse.success(instance);
    } catch (Exception e) {
      // TODO: Handle exception and print user friendly message
      return ApiResponse.error(INTERNAL_SERVER_ERROR, e.getMessage());
    }
  }

  public Result list(UUID customerUUID) {
    Customer customer = Customer.find.byId(customerUUID);

    if (customer == null) {
			return ApiResponse.error(BAD_REQUEST, "Invalid Customer UUID: " + customerUUID);
		}

    Set<Instance> instanceSet = customer.getInstances();

    return ApiResponse.success(instanceSet);
  }

  public Result update(UUID customerUUID) { return TODO; }

  private JsonNode submitCommissionerTask(Instance instanceInfo) {
    ObjectNode postData = Json.newObject();
		postData.put("instanceName", instanceInfo.name);
		postData.put("instanceUUID", instanceInfo.getInstanceId().toString());
		postData.put("create", true);
		postData.put("cloudProvider", "aws");
		postData.set("subnets", instanceInfo.getPlacementInfo().get("subnets"));

    String commissionerRESTUrl = "http://" + ctx().request().host() + "/commissioner/tasks";
		return apiHelper.postRequest(commissionerRESTUrl, postData);
  }
}
