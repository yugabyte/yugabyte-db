// Copyright (c) YugaByte, Inc.

package controllers.yb;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.inject.Inject;
import controllers.AuthenticatedController;
import forms.yb.CreateInstanceFormData;
import models.cloud.AvailabilityZone;
import models.yb.Customer;
import models.yb.Instance;
import play.data.Form;
import play.data.FormFactory;
import play.libs.Json;
import play.mvc.Result;

import java.util.*;

public class InstanceController extends AuthenticatedController {

	@Inject
	FormFactory formFactory;

	public Result create(UUID customerUUID) {
		Form<CreateInstanceFormData> formData = formFactory.form(CreateInstanceFormData.class).bindFromRequest();
		ObjectNode responseJson = Json.newObject();

		if (formData.hasErrors()) {
			responseJson.set("error", formData.errorsAsJson());
			return badRequest(responseJson);
		}

		Customer customer = Customer.find.byId(customerUUID);

		if (customer == null) {
			responseJson.put("error", "Invalid Customer UUID: " + customerUUID);
			return badRequest(responseJson);
		}

		ObjectNode placementInfo = Json.newObject();
		List<AvailabilityZone> azList = AvailabilityZone.find.select("subnet").where().eq("region_uuid", formData.get().regionUUID).findList();

		if (azList.isEmpty()) {
			responseJson.put("error", "No valid Availabilty Zone found for region: " + formData.get().regionUUID);
			return internalServerError(responseJson);
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
			Instance instance = Instance.create(customer, formData.get().name, Instance.ProvisioningState.Pending, placementInfo);
			return ok(Json.toJson(instance));
		} catch (Exception e) {
			// TODO: Handle exception and print user friendly message
			responseJson.put("error", e.getMessage());
			return internalServerError(responseJson);
		}
	}

	public Result list(UUID customerUUID) {
		Customer customer = Customer.find.byId(customerUUID);

		if (customer == null) {
			ObjectNode responseJson = Json.newObject();
			responseJson.put("error", "Invalid Customer UUID: " + customerUUID);
			return badRequest(responseJson);
		}

		Set<Instance> instanceSet = customer.getInstances();

		return ok(Json.toJson(instanceSet));
	}

	public Result update(UUID customerUUID) { return TODO; }

}
