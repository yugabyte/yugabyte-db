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

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.google.inject.Inject;
import com.yugabyte.yw.common.ApiResponse;
import com.yugabyte.yw.common.ValidatingFormFactory;
import com.yugabyte.yw.common.YWServiceException;
import com.yugabyte.yw.common.alerts.AlertDefinitionLabelsBuilder;
import com.yugabyte.yw.common.alerts.AlertDefinitionService;
import com.yugabyte.yw.common.alerts.AlertReceiverParams;
import com.yugabyte.yw.common.alerts.AlertUtils;
import com.yugabyte.yw.common.alerts.YWValidateException;
import com.yugabyte.yw.forms.AlertDefinitionFormData;
import com.yugabyte.yw.forms.AlertFormData;
import com.yugabyte.yw.forms.AlertReceiverFormData;
import com.yugabyte.yw.forms.AlertRouteFormData;
import com.yugabyte.yw.models.Alert;
import com.yugabyte.yw.models.AlertDefinition;
import com.yugabyte.yw.models.AlertReceiver;
import com.yugabyte.yw.models.AlertRoute;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.filters.AlertDefinitionFilter;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.data.Form;
import play.libs.Json;
import play.mvc.Result;

import java.util.List;
import java.util.UUID;

public class AlertController extends AuthenticatedController {
  public static final Logger LOG = LoggerFactory.getLogger(AlertController.class);

  @Inject private ValidatingFormFactory formFactory;

  @Inject private AlertDefinitionService alertDefinitionService;

  /** Lists alerts for given customer. */
  public Result list(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);

    ArrayNode alerts = Json.newArray();
    for (Alert alert : Alert.list(customerUUID)) {
      alerts.add(alert.toJson());
    }

    return ok(alerts);
  }

  public Result listActive(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);

    ArrayNode alerts = Json.newArray();
    for (Alert alert : Alert.listActive(customerUUID)) {
      alerts.add(alert.toJson());
    }
    return ok(alerts);
  }

  /**
   * Upserts alert of specified errCode with new message and createTime. Creates alert if needed.
   * This may only be used to create or update alerts that have 1 or fewer entries in the DB. e.g.
   * Creating two different alerts with errCode='LOW_ULIMITS' and then calling this would error.
   * Creating one alert with errCode=`LOW_ULIMITS` and then calling update would change the
   * previously created alert.
   */
  public Result upsert(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);

    Form<AlertFormData> formData = formFactory.getFormDataOrBadRequest(AlertFormData.class);

    AlertFormData data = formData.get();
    List<Alert> alerts = Alert.list(customerUUID, data.errCode);
    if (alerts.size() > 1) {
      return ApiResponse.error(
          CONFLICT,
          "May only update alerts that have been created once."
              + "Use POST instead to create new alert.");
    } else if (alerts.size() == 1) {
      alerts.get(0).update(data.message);
    } else {
      Alert.create(customerUUID, data.errCode, data.type, data.message);
    }
    auditService().createAuditEntry(ctx(), request(), Json.toJson(formData.data()));
    return ok();
  }

  /** Creates new alert. */
  public Result create(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);

    Form<AlertFormData> formData = formFactory.getFormDataOrBadRequest(AlertFormData.class);

    AlertFormData data = formData.get();
    Alert.create(customerUUID, data.errCode, data.type, data.message);
    auditService().createAuditEntry(ctx(), request(), Json.toJson(formData.data()));
    return ok();
  }

  public Result createDefinition(UUID customerUUID, UUID universeUUID) {

    Customer.getOrBadRequest(customerUUID);

    Form<AlertDefinitionFormData> formData =
        formFactory.getFormDataOrBadRequest(AlertDefinitionFormData.class);

    AlertDefinitionFormData data = formData.get();
    Universe universe = Universe.getOrBadRequest(universeUUID);

    AlertDefinition definition = new AlertDefinition();
    definition.setCustomerUUID(customerUUID);
    definition.setTargetType(AlertDefinition.TargetType.Universe);
    definition.setName(data.name);
    definition.setQuery(data.template.buildTemplate(universe.getUniverseDetails().nodePrefix));
    definition.setQueryThreshold(data.value);
    definition.setActive(true);
    definition.setLabels(AlertDefinitionLabelsBuilder.create().appendUniverse(universe).get());
    AlertDefinition createdDefinition = alertDefinitionService.create(definition);

    return ok(Json.toJson(createdDefinition));
  }

  public Result getAlertDefinition(UUID customerUUID, UUID universeUUID, String name) {
    Customer.getOrBadRequest(customerUUID);

    List<AlertDefinition> definitions =
        alertDefinitionService.list(
            new AlertDefinitionFilter()
                .setCustomerUuid(customerUUID)
                .setName(name)
                .setLabel(KnownAlertLabels.UNIVERSE_UUID, universeUUID.toString()));
    AlertDefinition definition =
        definitions
            .stream()
            .findFirst()
            .orElseThrow(
                () ->
                    new YWServiceException(
                        BAD_REQUEST,
                        name
                            + " alert definition for customer "
                            + customerUUID.toString()
                            + " and universe "
                            + universeUUID
                            + " not found"));

    return ok(Json.toJson(definition));
  }

  public Result updateAlertDefinition(UUID customerUUID, UUID alertDefinitionUUID) {

    Customer.getOrBadRequest(customerUUID);

    AlertDefinition definition = alertDefinitionService.getOrBadRequest(alertDefinitionUUID);

    Form<AlertDefinitionFormData> formData =
        formFactory.getFormDataOrBadRequest(AlertDefinitionFormData.class);

    AlertDefinitionFormData data = formData.get();

    AlertDefinition updatedDefinition;
    switch (definition.getTargetType()) {
      case Universe:
        Universe universe = Universe.getOrBadRequest(definition.getUniverseUUID());
        definition.setQuery(data.template.buildTemplate(universe.getUniverseDetails().nodePrefix));
        break;
      default:
        throw new IllegalStateException(
            "Unexpected definition type " + definition.getTargetType().name());
    }
    definition.setQueryThreshold(data.value);
    definition.setActive(data.active);
    updatedDefinition = alertDefinitionService.update(definition);
    return ok(Json.toJson(updatedDefinition));
  }

  private AlertReceiverFormData getFormData() {
    try {
      ObjectMapper mapper = new ObjectMapper();
      return mapper.treeToValue(request().body().asJson(), AlertReceiverFormData.class);
    } catch (RuntimeException | JsonProcessingException e) {
      throw new YWServiceException(BAD_REQUEST, "Invalid JSON");
    }
  }

  public Result createAlertReceiver(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);

    AlertReceiverFormData data = getFormData();
    AlertReceiver receiver = new AlertReceiver();
    receiver.setCustomerUuid(customerUUID);
    receiver.setUuid(UUID.randomUUID());
    receiver.setTargetType(data.targetType);
    receiver.setParams(
        data.params == null
            ? AlertUtils.createParamsInstance(data.targetType)
            : AlertUtils.fromJson(data.targetType, data.params));

    try {
      AlertUtils.validate(receiver);
    } catch (YWValidateException e) {
      throw new YWServiceException(
          BAD_REQUEST, "Unable to create alert receiver: " + e.getMessage());
    }

    receiver.save();
    auditService().createAuditEntryWithReqBody(ctx());
    return ok(Json.toJson(receiver));
  }

  public Result getAlertReceiver(UUID customerUUID, UUID alertReceiverUUID) {
    Customer.getOrBadRequest(customerUUID);
    return ok(Json.toJson(AlertReceiver.getOrBadRequest(customerUUID, alertReceiverUUID)));
  }

  public Result updateAlertReceiver(UUID customerUUID, UUID alertReceiverUUID) {
    Customer.getOrBadRequest(customerUUID);
    AlertReceiver receiver = AlertReceiver.getOrBadRequest(customerUUID, alertReceiverUUID);

    AlertReceiverFormData data = getFormData();
    AlertReceiverParams newParams = AlertUtils.fromJson(data.targetType, data.params);
    if (newParams == null) {
      throw new YWServiceException(
          BAD_REQUEST, "Unable to update alert receiver. Invalid parameters.");
    }

    receiver.setTargetType(data.targetType);
    receiver.setParams(newParams);

    try {
      AlertUtils.validate(receiver);
      receiver.save();
    } catch (YWValidateException e) {
      throw new YWServiceException(
          BAD_REQUEST, "Unable to update alert receiver: " + e.getMessage());
    }

    auditService().createAuditEntryWithReqBody(ctx());
    return ok(Json.toJson(receiver));
  }

  public Result deleteAlertReceiver(UUID customerUUID, UUID alertReceiverUUID) {
    Customer.getOrBadRequest(customerUUID);
    AlertReceiver receiver = AlertReceiver.getOrBadRequest(customerUUID, alertReceiverUUID);
    LOG.info("Deleting alert receiver {} for customer {}", receiver.getUuid(), customerUUID);

    if (!receiver.delete()) {
      throw new YWServiceException(
          INTERNAL_SERVER_ERROR, "Unable to delete alert receiver: " + receiver.getUuid());
    }

    auditService().createAuditEntry(ctx(), request());
    return ok();
  }

  public Result listAlertReceivers(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);
    return ok(Json.toJson(AlertReceiver.list(customerUUID)));
  }

  public Result createAlertRoute(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);
    AlertRouteFormData data = formFactory.getFormDataOrBadRequest(AlertRouteFormData.class).get();
    try {
      AlertRoute route = AlertRoute.create(customerUUID, data.definitionUUID, data.receiverUUID);
      auditService().createAuditEntryWithReqBody(ctx());
      return ok(Json.toJson(route));
    } catch (Exception e) {
      throw new YWServiceException(BAD_REQUEST, "Unable to create alert route.");
    }
  }

  public Result getAlertRoute(UUID customerUUID, UUID alertRouteUUID) {
    Customer.getOrBadRequest(customerUUID);
    AlertRoute route = AlertRoute.getOrBadRequest(alertRouteUUID);
    AlertDefinition definition = alertDefinitionService.getOrBadRequest(route.getDefinitionUUID());
    if ((definition == null) || !customerUUID.equals(definition.getCustomerUUID())) {
      throw new YWServiceException(BAD_REQUEST, "Invalid Alert Route UUID: " + route.getUuid());
    }
    return ok(Json.toJson(route));
  }

  public Result deleteAlertRoute(UUID customerUUID, UUID alertRouteUUID) {
    Customer.getOrBadRequest(customerUUID);
    AlertRoute route = AlertRoute.getOrBadRequest(alertRouteUUID);
    LOG.info("Deleting alert route {} for customer {}", route.getUuid(), customerUUID);

    if (!route.delete()) {
      throw new YWServiceException(
          INTERNAL_SERVER_ERROR, "Unable to delete alert route: " + route.getUuid());
    }

    auditService().createAuditEntry(ctx(), request());
    return ok();
  }

  public Result listAlertRoutes(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);
    return ok(Json.toJson(AlertRoute.listByCustomer(customerUUID)));
  }
}
