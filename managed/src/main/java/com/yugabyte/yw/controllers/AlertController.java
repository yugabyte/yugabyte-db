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
import com.yugabyte.yw.common.YWServiceException;
import com.yugabyte.yw.common.alerts.*;
import com.yugabyte.yw.forms.AlertDefinitionFormData;
import com.yugabyte.yw.forms.AlertReceiverFormData;
import com.yugabyte.yw.forms.AlertRouteFormData;
import com.yugabyte.yw.models.*;
import com.yugabyte.yw.models.filters.AlertDefinitionFilter;
import com.yugabyte.yw.models.filters.AlertFilter;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import lombok.extern.slf4j.Slf4j;
import play.data.Form;
import play.libs.Json;
import play.mvc.Result;

import java.util.List;
import java.util.UUID;

@Slf4j
public class AlertController extends AuthenticatedController {

  @Inject private AlertDefinitionService alertDefinitionService;

  @Inject private AlertService alertService;

  /** Lists alerts for given customer. */
  public Result list(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);

    ArrayNode alerts = Json.newArray();
    AlertFilter filter = AlertFilter.builder().customerUuid(customerUUID).build();
    alertService.list(filter).forEach(alert -> alerts.add(Json.toJson(alert)));
    return ok(alerts);
  }

  public Result listActive(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);

    ArrayNode alerts = Json.newArray();
    AlertFilter filter = AlertFilter.builder().customerUuid(customerUUID).build();
    alertService.listNotResolved(filter).forEach(alert -> alerts.add(Json.toJson(alert)));
    return ok(alerts);
  }

  public Result createDefinition(UUID customerUUID, UUID universeUUID) {

    Customer.getOrBadRequest(customerUUID);

    Form<AlertDefinitionFormData> formData =
        formFactory.getFormDataOrBadRequest(AlertDefinitionFormData.class);

    AlertDefinitionFormData data = formData.get();
    Universe universe = Universe.getOrBadRequest(universeUUID);

    AlertDefinition definition =
        new AlertDefinition()
            .setCustomerUUID(customerUUID)
            .setName(data.name)
            .setQuery(data.template.buildTemplate(universe.getUniverseDetails().nodePrefix))
            .setQueryThreshold(data.value)
            .setActive(true)
            .setLabels(AlertDefinitionLabelsBuilder.create().appendTarget(universe).get());
    AlertDefinition createdDefinition = alertDefinitionService.create(definition);

    return ok(Json.toJson(createdDefinition));
  }

  public Result getAlertDefinition(UUID customerUUID, UUID universeUUID, String name) {
    Customer.getOrBadRequest(customerUUID);

    List<AlertDefinition> definitions =
        alertDefinitionService.list(
            AlertDefinitionFilter.builder()
                .customerUuid(customerUUID)
                .name(name)
                .label(KnownAlertLabels.UNIVERSE_UUID, universeUUID.toString())
                .build());
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

    AlertDefinitionFormData data =
        formFactory.getFormDataOrBadRequest(AlertDefinitionFormData.class).get();

    // For now only templates related to universe are supported.
    Universe universe = Universe.getOrBadRequest(definition.getUniverseUUID());
    definition.setQuery(data.template.buildTemplate(universe.getUniverseDetails().nodePrefix));
    definition.setQueryThreshold(data.value);
    definition.setActive(data.active);
    AlertDefinition updatedDefinition = alertDefinitionService.update(definition);
    return ok(Json.toJson(updatedDefinition));
  }

  /**
   * This function is needed to properly deserialize dynamic type of the params field.
   *
   * @return
   */
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
    receiver.setCustomerUUID(customerUUID);
    receiver.setUuid(UUID.randomUUID());
    receiver.setName(data.name);
    receiver.setParams(data.params);

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
    receiver.setName(data.name);
    receiver.setParams(data.params);

    try {
      AlertUtils.validate(receiver);
    } catch (YWValidateException e) {
      throw new YWServiceException(
          BAD_REQUEST, "Unable to update alert receiver: " + e.getMessage());
    }

    receiver.save();
    auditService().createAuditEntryWithReqBody(ctx());
    return ok(Json.toJson(receiver));
  }

  public Result deleteAlertReceiver(UUID customerUUID, UUID alertReceiverUUID) {
    Customer.getOrBadRequest(customerUUID);
    AlertReceiver receiver = AlertReceiver.getOrBadRequest(customerUUID, alertReceiverUUID);
    log.info("Deleting alert receiver {} for customer {}", alertReceiverUUID, customerUUID);

    if (!receiver.delete()) {
      throw new YWServiceException(
          INTERNAL_SERVER_ERROR, "Unable to delete alert receiver: " + alertReceiverUUID);
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
    List<AlertReceiver> receivers = AlertReceiver.getOrBadRequest(customerUUID, data.receivers);
    try {
      AlertRoute route = AlertRoute.create(customerUUID, data.name, receivers);
      auditService().createAuditEntryWithReqBody(ctx());
      return ok(Json.toJson(route));
    } catch (Exception e) {
      throw new YWServiceException(BAD_REQUEST, "Unable to create alert route: " + e.getMessage());
    }
  }

  public Result getAlertRoute(UUID customerUUID, UUID alertRouteUUID) {
    Customer.getOrBadRequest(customerUUID);
    return ok(Json.toJson(AlertRoute.getOrBadRequest(customerUUID, alertRouteUUID)));
  }

  public Result updateAlertRoute(UUID customerUUID, UUID alertRouteUUID) {
    Customer.getOrBadRequest(customerUUID);
    AlertRouteFormData data = formFactory.getFormDataOrBadRequest(AlertRouteFormData.class).get();
    AlertRoute route = AlertRoute.getOrBadRequest(customerUUID, alertRouteUUID);
    List<AlertReceiver> receivers = AlertReceiver.getOrBadRequest(customerUUID, data.receivers);
    try {
      route.setName(data.name);
      route.setReceiversList(receivers);
      route.save();

      auditService().createAuditEntryWithReqBody(ctx());
      return ok(Json.toJson(route));
    } catch (Exception e) {
      throw new YWServiceException(BAD_REQUEST, "Unable to update alert route: " + e.getMessage());
    }
  }

  public Result deleteAlertRoute(UUID customerUUID, UUID alertRouteUUID) {
    Customer.getOrBadRequest(customerUUID);
    AlertRoute route = AlertRoute.getOrBadRequest(customerUUID, alertRouteUUID);
    log.info("Deleting alert route {} for customer {}", alertRouteUUID, customerUUID);

    if (!route.delete()) {
      throw new YWServiceException(
          INTERNAL_SERVER_ERROR, "Unable to delete alert route: " + alertRouteUUID);
    }

    auditService().createAuditEntry(ctx(), request());
    return ok();
  }

  public Result listAlertRoutes(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);
    return ok(Json.toJson(AlertRoute.listByCustomer(customerUUID)));
  }
}
