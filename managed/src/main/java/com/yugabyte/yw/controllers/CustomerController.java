// Copyright 2020 YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.models.helpers.CommonUtils.datePlus;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableList;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.CloudQueryHelper;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.alerts.AlertConfigurationService;
import com.yugabyte.yw.common.alerts.AlertService;
import com.yugabyte.yw.common.metrics.MetricService;
import com.yugabyte.yw.forms.AlertingFormData;
import com.yugabyte.yw.forms.AlertingFormData.AlertingData;
import com.yugabyte.yw.forms.CustomerDetailsData;
import com.yugabyte.yw.forms.FeatureUpdateFormData;
import com.yugabyte.yw.forms.MetricQueryParams;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPError;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.metrics.MetricQueryHelper;
import com.yugabyte.yw.models.Alert;
import com.yugabyte.yw.models.Alert.State;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerConfig;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.extended.UserWithFeatures;
import com.yugabyte.yw.models.filters.AlertFilter;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.NodeDetails;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiImplicitParam;
import io.swagger.annotations.ApiImplicitParams;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.ApiResponses;
import io.swagger.annotations.Authorization;
import java.time.temporal.ChronoUnit;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.data.Form;
import play.libs.Json;
import play.mvc.Result;

@Api(
    value = "Customer management",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class CustomerController extends AuthenticatedController {

  public static final Logger LOG = LoggerFactory.getLogger(CustomerController.class);

  @Inject private AlertService alertService;

  @Inject private MetricService metricService;

  @Inject private MetricQueryHelper metricQueryHelper;

  @Inject private CloudQueryHelper cloudQueryHelper;

  @Inject private AlertConfigurationService alertConfigurationService;

  private static boolean checkNonNullMountRoots(NodeDetails n) {
    return n.cloudInfo != null
        && n.cloudInfo.mount_roots != null
        && !n.cloudInfo.mount_roots.isEmpty();
  }

  @Deprecated
  @ApiOperation(
      value = "UI_ONLY",
      response = UUID.class,
      responseContainer = "List",
      hidden = true,
      nickname = "ListOfCustomersUUID")
  public Result listUuids() {
    ArrayNode responseJson = Json.newArray();
    Customer.getAll().forEach(c -> responseJson.add(c.getUuid().toString()));
    return ok(responseJson);
  }

  @ApiOperation(
      value = "List customers",
      response = Customer.class,
      responseContainer = "List",
      nickname = "ListOfCustomers")
  public Result list() {
    return PlatformResults.withData(Customer.getAll());
  }

  @ApiOperation(
      value = "Get a customer's details",
      response = CustomerDetailsData.class,
      nickname = "CustomerDetail")
  public Result index(UUID customerUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);

    ObjectNode responseJson = (ObjectNode) Json.toJson(customer);
    CustomerConfig config = CustomerConfig.getAlertConfig(customerUUID);
    // TODO: get rid of this
    if (config != null) {
      responseJson.set("alertingData", config.getMaskedData());
    } else {
      responseJson.set("alertingData", null);
    }
    CustomerConfig smtpConfig = CustomerConfig.getSmtpConfig(customerUUID);
    if (smtpConfig != null) {
      responseJson.set("smtpData", smtpConfig.getMaskedData());
    } else {
      responseJson.set("smtpData", null);
    }
    responseJson.put(
        "callhomeLevel", CustomerConfig.getOrCreateCallhomeLevel(customerUUID).toString());

    UserWithFeatures user = (UserWithFeatures) ctx().args.get("user");
    if (customer.getFeatures().size() != 0 && user.getFeatures().size() != 0) {
      JsonNode featureSet = user.getFeatures();
      CommonUtils.deepMerge(featureSet, customer.getFeatures());
      responseJson.put("features", featureSet);
    } else if (customer.getFeatures().size() != 0) {
      responseJson.put("features", customer.getFeatures());
    } else {
      responseJson.put("features", user.getFeatures());
    }

    return ok(responseJson);
  }

  @ApiOperation(value = "Update a customer", response = Customer.class, nickname = "UpdateCustomer")
  @ApiImplicitParams({
    @ApiImplicitParam(
        name = "Customer",
        value = "Customer data to be updated",
        required = true,
        dataType = "com.yugabyte.yw.forms.AlertingFormData",
        paramType = "body")
  })
  public Result update(UUID customerUUID) {

    Customer customer = Customer.getOrBadRequest(customerUUID);

    JsonNode request = request().body().asJson();
    Form<AlertingFormData> formData = formFactory.getFormDataOrBadRequest(AlertingFormData.class);
    AlertingFormData alertingFormData = formData.get();

    if (alertingFormData.name != null) {
      customer.name = alertingFormData.name;
      customer.save();
    }

    if (request.has("alertingData") || request.has("smtpData")) {

      CustomerConfig config = CustomerConfig.getAlertConfig(customerUUID);
      if (alertingFormData.alertingData != null) {
        long activeAlertNotificationPeriod =
            alertingFormData.alertingData.activeAlertNotificationIntervalMs;
        long oldActiveAlertNotificationPeriod = 0;
        if (config == null) {
          CustomerConfig.createAlertConfig(
              customerUUID, Json.toJson(alertingFormData.alertingData));
        } else {
          AlertingData oldData = Json.fromJson(config.getData(), AlertingData.class);
          if (oldData != null) {
            oldActiveAlertNotificationPeriod = oldData.activeAlertNotificationIntervalMs;
          }
          config.unmaskAndSetData((ObjectNode) Json.toJson(alertingFormData.alertingData));
          config.update();
        }

        if (activeAlertNotificationPeriod != oldActiveAlertNotificationPeriod) {
          AlertFilter alertFilter =
              AlertFilter.builder().customerUuid(customerUUID).state(State.ACTIVE).build();
          List<Alert> activeAlerts = alertService.list(alertFilter);
          List<Alert> alertsToUpdate;
          if (activeAlertNotificationPeriod > 0) {
            // In case there was notification attempt - setting to last attempt time
            // + interval as next notification attempt. Even if it's before now -
            // instant notification will happen - which is what we need.
            alertsToUpdate =
                activeAlerts
                    .stream()
                    .filter(
                        alert ->
                            alert.getNextNotificationTime() == null
                                && alert.getNotificationAttemptTime() != null)
                    .map(
                        alert ->
                            alert.setNextNotificationTime(
                                datePlus(
                                    alert.getNotificationAttemptTime(),
                                    activeAlertNotificationPeriod,
                                    ChronoUnit.MILLIS)))
                    .collect(Collectors.toList());
          } else {
            // In case we already notified on ACTIVE state and scheduled subsequent notification
            // - clean that
            alertsToUpdate =
                activeAlerts
                    .stream()
                    .filter(
                        alert ->
                            alert.getNextNotificationTime() != null
                                && alert.getNotifiedState() != null)
                    .map(alert -> alert.setNextNotificationTime(null))
                    .collect(Collectors.toList());
          }
          alertService.save(alertsToUpdate);
        }
      }

      CustomerConfig smtpConfig = CustomerConfig.getSmtpConfig(customerUUID);
      if (smtpConfig == null && alertingFormData.smtpData != null) {
        CustomerConfig.createSmtpConfig(customerUUID, Json.toJson(alertingFormData.smtpData));
      } else if (smtpConfig != null && alertingFormData.smtpData != null) {
        smtpConfig.unmaskAndSetData((ObjectNode) Json.toJson(alertingFormData.smtpData));
        smtpConfig.update();
      } // In case we want to reset the smtpData and use the default mailing server.
      else if (request.has("smtpData") && alertingFormData.smtpData == null) {
        if (smtpConfig != null) {
          smtpConfig.delete();
        }
      }
    }

    // Features would be a nested json, so we should fetch it differently.
    JsonNode requestBody = request().body().asJson();
    if (requestBody.has("features")) {
      customer.upsertFeatures(requestBody.get("features"));
    }

    CustomerConfig.upsertCallhomeConfig(customerUUID, alertingFormData.callhomeLevel);

    return ok(Json.toJson(customer));
  }

  @ApiOperation(
      value = "Delete a customer",
      response = YBPSuccess.class,
      nickname = "deleteCustomer")
  public Result delete(UUID customerUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);

    List<Users> users = Users.getAll(customerUUID);
    for (Users user : users) {
      user.delete();
    }

    if (!customer.delete()) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Unable to delete Customer UUID: " + customerUUID);
    }

    metricService.handleSourceRemoval(customerUUID, null);

    auditService().createAuditEntry(ctx(), request());
    return YBPSuccess.empty();
  }

  @ApiOperation(
      value = "Create or update a customer's features",
      hidden = true,
      responseContainer = "Map",
      response = Object.class)
  @ApiImplicitParams({
    @ApiImplicitParam(
        name = "Feature",
        value = "Feature to be upserted",
        required = true,
        dataType = "com.yugabyte.yw.forms.FeatureUpdateFormData",
        paramType = "body")
  })
  public Result upsertFeatures(UUID customerUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);

    JsonNode requestBody = request().body().asJson();
    ObjectMapper mapper = new ObjectMapper();
    FeatureUpdateFormData formData;
    try {
      formData = mapper.treeToValue(requestBody, FeatureUpdateFormData.class);
    } catch (RuntimeException | JsonProcessingException e) {
      throw new PlatformServiceException(BAD_REQUEST, "Invalid JSON");
    }

    customer.upsertFeatures(formData.features);

    auditService().createAuditEntry(ctx(), request(), requestBody);
    return ok(customer.getFeatures());
  }

  @ApiOperation(
      value = "Add metrics to a customer",
      response = Object.class,
      responseContainer = "Map")
  @ApiResponses(
      @io.swagger.annotations.ApiResponse(
          code = BAD_REQUEST,
          message = "When request fails validations.",
          response = YBPError.class))
  @ApiImplicitParams({
    @ApiImplicitParam(
        name = "Metrics",
        value = "Metrics to be added",
        required = true,
        dataType = "com.yugabyte.yw.forms.MetricQueryParams",
        paramType = "body")
  })
  public Result metrics(UUID customerUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);

    Form<MetricQueryParams> formData = formFactory.getFormDataOrBadRequest(MetricQueryParams.class);

    Map<String, String> params = new HashMap<>(formData.rawData());
    HashMap<String, Map<String, String>> filterOverrides = new HashMap<>();
    // Given we have a limitation on not being able to rename the pod labels in
    // kubernetes cadvisor metrics, we try to see if the metric being queried is for
    // container or not, and use pod_name vs exported_instance accordingly.
    // Expect for container metrics, all the metrics would with node_prefix and exported_instance.
    boolean hasContainerMetric =
        formData.get().getMetrics().stream().anyMatch(s -> s.startsWith("container"));
    String universeFilterLabel = hasContainerMetric ? "namespace" : "node_prefix";
    String nodeFilterLabel = hasContainerMetric ? "pod_name" : "exported_instance";
    String containerLabel = "container_name";
    String pvcLabel = "persistentvolumeclaim";

    ObjectNode filterJson = Json.newObject();
    if (!params.containsKey("nodePrefix")) {
      String universePrefixes =
          customer
              .getUniverses()
              .stream()
              .map((universe -> universe.getUniverseDetails().nodePrefix))
              .collect(Collectors.joining("|"));
      filterJson.put(universeFilterLabel, String.join("|", universePrefixes));
    } else {
      // Check if it is a Kubernetes deployment.
      if (hasContainerMetric) {
        final String nodePrefix = params.remove("nodePrefix");
        if (params.containsKey("nodeName")) {
          // We calculate the correct namespace by using the zone if
          // it exists. Example: it is yb-tserver-0_az1 (multi AZ) or
          // yb-tserver-0 (single AZ).
          String[] nodeWithZone = params.remove("nodeName").split("_");
          filterJson.put(nodeFilterLabel, nodeWithZone[0]);
          // TODO(bhavin192): might need to account for multiple
          // releases in one namespace.
          // The pod name is of the format yb-<server>-<replica_num> and we just need the
          // container, which is yb-<server>.
          String containerName = nodeWithZone[0].substring(0, nodeWithZone[0].lastIndexOf("-"));
          String pvcName = String.format("(.*)-%s", nodeWithZone[0]);
          String azName = nodeWithZone.length == 2 ? nodeWithZone[1] : null;

          filterJson.put(containerLabel, containerName);
          filterJson.put(pvcLabel, pvcName);
          filterJson.put(universeFilterLabel, getNamespacesFilter(customer, nodePrefix, azName));
        } else {
          filterJson.put(universeFilterLabel, getNamespacesFilter(customer, nodePrefix));
        }
      } else {
        final String nodePrefix = params.remove("nodePrefix");
        filterJson.put(universeFilterLabel, nodePrefix);
        if (params.containsKey("nodeName")) {
          filterJson.put(nodeFilterLabel, params.remove("nodeName"));
        }

        filterOverrides.putAll(getFilterOverrides(customer, nodePrefix, formData.get()));
      }
    }
    if (params.containsKey("tableName")) {
      filterJson.put("table_name", params.remove("tableName"));
    }
    params.put("filters", Json.stringify(filterJson));
    JsonNode response;
    if (formData.get().getIsRecharts()) {
      response =
          metricQueryHelper.query(
              formData.get().getMetrics(), params, filterOverrides, formData.get().getIsRecharts());
    } else {
      response = metricQueryHelper.query(formData.get().getMetrics(), params, filterOverrides);
    }

    if (response.has("error")) {
      throw new PlatformServiceException(BAD_REQUEST, response.get("error"));
    }
    return PlatformResults.withRawData(response);
  }

  private String getNamespacesFilter(Customer customer, String nodePrefix) {
    return getNamespacesFilter(customer, nodePrefix, null);
  }

  // Return a regex string for filtering the metrics based on
  // namespaces of the universe matching the given customer and
  // nodePrefix. If azName is not null, then returns the only
  // namespace corresponding to the given AZ. Should be used for
  // Kubernetes universes only.
  private String getNamespacesFilter(Customer customer, String nodePrefix, String azName) {
    // We need to figure out the correct namespace for each AZ.  We do
    // that by getting the correct universe and its provider and then
    // go through the azConfigs.
    List<Universe> universes =
        customer
            .getUniverses()
            .stream()
            .filter(u -> u.getUniverseDetails().nodePrefix.equals(nodePrefix))
            .collect(Collectors.toList());
    // TODO: account for readonly replicas when we support them for
    // Kubernetes providers.
    Provider provider =
        Provider.get(
            UUID.fromString(
                universes.get(0).getUniverseDetails().getPrimaryCluster().userIntent.provider));
    List<String> namespaces = new ArrayList<>();
    boolean isMultiAZ = PlacementInfoUtil.isMultiAZ(provider);

    for (Region r : Region.getByProvider(provider.uuid)) {
      for (AvailabilityZone az : AvailabilityZone.getAZsForRegion(r.uuid)) {
        if (azName != null && !azName.equals(az.code)) {
          continue;
        }
        namespaces.add(
            PlacementInfoUtil.getKubernetesNamespace(
                isMultiAZ, nodePrefix, az.code, az.getUnmaskedConfig()));
      }
    }

    return String.join("|", namespaces);
  }

  @ApiOperation(
      value = "Get a customer's host info",
      responseContainer = "Map",
      response = Object.class)
  public Result getHostInfo(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);
    ObjectNode hostInfo = Json.newObject();
    hostInfo.put(
        Common.CloudType.aws.name(),
        cloudQueryHelper.currentHostInfo(
            Common.CloudType.aws,
            ImmutableList.of("instance-id", "vpc-id", "privateIp", "region")));
    hostInfo.put(
        Common.CloudType.gcp.name(), cloudQueryHelper.currentHostInfo(Common.CloudType.gcp, null));

    return PlatformResults.withRawData(hostInfo);
  }

  private HashMap<String, HashMap<String, String>> getFilterOverrides(
      Customer customer, String nodePrefix, MetricQueryParams mqParams) {

    HashMap<String, HashMap<String, String>> filterOverrides = new HashMap<>();
    // For a disk usage metric query, the mount point has to be modified to match the actual
    // mount point for an onprem universe.
    if (mqParams.getMetrics().contains("disk_usage")) {
      List<Universe> universes =
          customer
              .getUniverses()
              .stream()
              .filter(
                  u ->
                      u.getUniverseDetails().nodePrefix != null
                          && u.getUniverseDetails().nodePrefix.equals(nodePrefix))
              .collect(Collectors.toList());
      if (universes.get(0).getUniverseDetails().getPrimaryCluster().userIntent.providerType
          == CloudType.onprem) {
        final String mountRoots =
            universes
                .get(0)
                .getNodes()
                .stream()
                .filter(CustomerController::checkNonNullMountRoots)
                .map(n -> n.cloudInfo.mount_roots)
                .findFirst()
                .orElse("");
        // TODO: technically, this code is based on the primary cluster being onprem
        // and will return inaccurate results if the universe has a read replica that is
        // not onprem.
        if (!mountRoots.isEmpty()) {
          HashMap<String, String> mountFilters = new HashMap<>();
          mountFilters.put("mountpoint", mountRoots.replace(',', '|'));
          // convert "/storage1,/bar" to the filter "/storage1|/bar"
          filterOverrides.put("disk_usage", mountFilters);
        } else {
          LOG.debug("No mount points found in onprem universe {}", nodePrefix);
        }
      }
    }
    return filterOverrides;
  }
}
