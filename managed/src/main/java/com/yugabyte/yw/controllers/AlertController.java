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

import com.fasterxml.jackson.databind.node.ArrayNode;
import com.google.inject.Inject;
import com.yugabyte.yw.common.ApiResponse;
import com.yugabyte.yw.common.ValidatingFormFactory;
import com.yugabyte.yw.common.YWServiceException;
import com.yugabyte.yw.common.alerts.AlertDefinitionLabelsBuilder;
import com.yugabyte.yw.common.alerts.AlertDefinitionService;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import com.yugabyte.yw.forms.AlertDefinitionFormData;
import com.yugabyte.yw.forms.AlertFormData;
import com.yugabyte.yw.models.Alert;
import com.yugabyte.yw.models.AlertDefinition;
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

  @Inject ValidatingFormFactory formFactory;

  @Inject private SettableRuntimeConfigFactory configFactory;

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
      Alert alert = Alert.create(customerUUID, data.errCode, data.type, data.message);
    }
    auditService().createAuditEntry(ctx(), request(), Json.toJson(formData.data()));
    return ok();
  }

  /** Creates new alert. */
  public Result create(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);

    Form<AlertFormData> formData = formFactory.getFormDataOrBadRequest(AlertFormData.class);

    AlertFormData data = formData.get();
    Alert alert = Alert.create(customerUUID, data.errCode, data.type, data.message);
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
}
