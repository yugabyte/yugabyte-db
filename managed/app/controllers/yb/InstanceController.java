// Copyright (c) YugaByte, Inc.

package controllers.yb;

import java.util.ArrayList;
import java.util.List;
import java.util.Set;
import java.util.UUID;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableMap;
import com.google.inject.Inject;
import com.typesafe.config.Config;
import com.typesafe.config.ConfigFactory;

import controllers.AuthenticatedController;
import forms.yb.CreateInstanceFormData;
import forms.yb.GrafanaPanelData;
import helpers.ApiHelper;
import helpers.ApiResponse;
import models.cloud.AvailabilityZone;
import models.yb.Customer;
import models.yb.CustomerTask;
import models.yb.Instance;
import play.data.Form;
import play.data.FormFactory;
import play.libs.Json;
import play.mvc.Result;
import views.html.*;

public class InstanceController extends AuthenticatedController {

  @Inject
  FormFactory formFactory;

  @Inject
  ApiHelper apiHelper;

  protected static final String GRAFANA_API_ENDPOINT = "/api/dashboards/db/yugabyte-cluster";
  protected static final String GRAFANA_DASHBOARD_URL = "/dashboard-solo/db/yugabyte-cluster";

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
    postData.put("create", true);
    postData.put("cloudProvider", "aws");
    postData.set("subnets", instanceInfo.getPlacementInfo().get("subnets"));

    String commissionerRESTUrl = "http://" + ctx().request().host() + "/commissioner/instances/" +
                                 instanceInfo.getInstanceId().toString();
    return apiHelper.postRequest(commissionerRESTUrl, postData);
  }
  public Result index(UUID instanceUUID) {
    long toTime = System.currentTimeMillis();
    long fromTime = toTime - 60 * 60 * 1000;

    Instance instance = Instance.find.where().eq("instance_id", instanceUUID).findUnique();
    Config conf = ConfigFactory.load();
    String grafanaUrl = conf.getString("yb.grafana.url") + GRAFANA_API_ENDPOINT;
    String grafanaAccessKey = conf.getString("yb.grafana.accessKey");
    List<GrafanaPanelData> grafanaPanelDataList = new ArrayList<GrafanaPanelData>();

    if (grafanaUrl != null && grafanaAccessKey != null) {
      JsonNode grafanaData =
				apiHelper.getRequest(grafanaUrl, ImmutableMap.of("Authorization", "Bearer " + grafanaAccessKey));
      for (JsonNode panelEntries : grafanaData.findValues("panels")) {
        for (final JsonNode entry : panelEntries) {
          GrafanaPanelData data = new GrafanaPanelData();
          data.id = entry.get("id").asInt(0);
          data.title = entry.get("title").asText();
          data.url = conf.getString("yb.grafana.url")
            + GRAFANA_DASHBOARD_URL
            + "?panelId=" + data.id
            + "&from=" + fromTime
            + "&to=" + toTime
            + "&var-host=" + instance.name;
          grafanaPanelDataList.add(data);
        }
      }
    }
    return ok(showInstance.render(instance, grafanaPanelDataList));
  }
}
