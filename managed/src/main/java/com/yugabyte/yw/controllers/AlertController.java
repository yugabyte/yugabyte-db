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
import com.yugabyte.yw.common.AlertDefinitionTemplate;
import com.yugabyte.yw.common.YWServiceException;
import com.yugabyte.yw.common.alerts.*;
import com.yugabyte.yw.forms.AlertReceiverFormData;
import com.yugabyte.yw.forms.AlertRouteFormData;
import com.yugabyte.yw.forms.YWResults;
import com.yugabyte.yw.models.*;
import com.yugabyte.yw.models.filters.AlertDefinitionGroupFilter;
import com.yugabyte.yw.models.filters.AlertDefinitionTemplateFilter;
import com.yugabyte.yw.models.filters.AlertFilter;
import com.yugabyte.yw.models.paging.AlertDefinitionGroupPagedQuery;
import com.yugabyte.yw.models.paging.AlertDefinitionGroupPagedResponse;
import io.swagger.annotations.*;
import lombok.extern.slf4j.Slf4j;
import play.libs.Json;
import play.mvc.Result;

import java.util.Arrays;
import java.util.List;
import java.util.UUID;
import java.util.stream.Collectors;

@Slf4j
@Api(value = "Alert", authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class AlertController extends AuthenticatedController {

  @Inject private AlertDefinitionGroupService alertDefinitionGroupService;

  @Inject private AlertService alertService;

  /** Lists alerts for given customer. */
  @ApiOperation(value = "listAlerts", response = Alert.class, responseContainer = "List")
  public Result list(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);

    ArrayNode alerts = Json.newArray();
    AlertFilter filter = AlertFilter.builder().customerUuid(customerUUID).build();
    alertService.list(filter).forEach(alert -> alerts.add(Json.toJson(alert)));
    return YWResults.withData(alerts);
  }

  @ApiOperation(value = "listActiveAlerts", response = Alert.class, responseContainer = "List")
  public Result listActive(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);

    ArrayNode alerts = Json.newArray();
    AlertFilter filter = AlertFilter.builder().customerUuid(customerUUID).build();
    alertService.listNotResolved(filter).forEach(alert -> alerts.add(Json.toJson(alert)));
    return YWResults.withData(alerts);
  }

  @ApiOperation(value = "getDefinitionGroup", response = AlertDefinitionGroup.class)
  public Result getDefinitionGroup(UUID customerUUID, UUID groupUUID) {
    Customer.getOrBadRequest(customerUUID);

    AlertDefinitionGroup group = alertDefinitionGroupService.getOrBadRequest(groupUUID);

    return YWResults.withData(group);
  }

  @ApiOperation(
      value = "listDefinitionGroupTemplates",
      response = AlertDefinitionGroup.class,
      responseContainer = "List")
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "ListTemplatesRequest",
          paramType = "body",
          dataType = "com.yugabyte.yw.models.filters.AlertDefinitionTemplateFilter",
          required = true))
  public Result listDefinitionGroupTemplates(UUID customerUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);

    AlertDefinitionTemplateFilter filter =
        Json.fromJson(request().body().asJson(), AlertDefinitionTemplateFilter.class);

    List<AlertDefinitionGroup> groups =
        Arrays.stream(AlertDefinitionTemplate.values())
            .filter(filter::matches)
            .map(
                template -> alertDefinitionGroupService.createGroupFromTemplate(customer, template))
            .collect(Collectors.toList());

    return YWResults.withData(groups);
  }

  @ApiOperation(value = "pageDefinitionGroups", response = AlertDefinitionGroupPagedResponse.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "PageDefinitionGroupsRequest",
          paramType = "body",
          dataType = "com.yugabyte.yw.models.paging.AlertDefinitionGroupPagedQuery",
          required = true))
  public Result pageDefinitionGroups(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);

    AlertDefinitionGroupPagedQuery query =
        Json.fromJson(request().body().asJson(), AlertDefinitionGroupPagedQuery.class);
    query.setFilter(query.getFilter().toBuilder().customerUuid(customerUUID).build());

    AlertDefinitionGroupPagedResponse groups = alertDefinitionGroupService.pagedList(query);

    return YWResults.withData(groups);
  }

  @ApiOperation(
      value = "listDefinitionGroups",
      response = AlertDefinitionGroup.class,
      responseContainer = "List")
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "ListGroupsRequest",
          paramType = "body",
          dataType = "com.yugabyte.yw.models.filters.AlertDefinitionGroupFilter",
          required = true))
  public Result listDefinitionGroups(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);

    AlertDefinitionGroupFilter filter =
        Json.fromJson(request().body().asJson(), AlertDefinitionGroupFilter.class);

    List<AlertDefinitionGroup> groups = alertDefinitionGroupService.list(filter);

    return YWResults.withData(groups);
  }

  @ApiOperation(value = "createDefinitionGroup", response = AlertDefinitionGroup.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "CreateGroupRequest",
          paramType = "body",
          dataType = "com.yugabyte.yw.models.AlertDefinitionGroup",
          required = true))
  public Result createDefinitionGroup(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);

    AlertDefinitionGroup group =
        Json.fromJson(request().body().asJson(), AlertDefinitionGroup.class);

    if (group.getUuid() != null) {
      throw new YWServiceException(BAD_REQUEST, "Can't create group with uuid set");
    }

    group = alertDefinitionGroupService.save(group);

    auditService().createAuditEntry(ctx(), request());
    return YWResults.withData(group);
  }

  @ApiOperation(value = "updateDefinitionGroup", response = AlertDefinitionGroup.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "UpdateGroupRequest",
          paramType = "body",
          dataType = "com.yugabyte.yw.models.AlertDefinitionGroup",
          required = true))
  public Result updateDefinitionGroup(UUID customerUUID, UUID groupUUID) {
    Customer.getOrBadRequest(customerUUID);
    alertDefinitionGroupService.getOrBadRequest(groupUUID);

    AlertDefinitionGroup group =
        Json.fromJson(request().body().asJson(), AlertDefinitionGroup.class);

    if (group.getUuid() == null) {
      throw new YWServiceException(BAD_REQUEST, "Can't update group with missing uuid");
    }

    if (!group.getUuid().equals(groupUUID)) {
      throw new YWServiceException(
          BAD_REQUEST, "Group UUID from path should be consistent with body");
    }

    group = alertDefinitionGroupService.save(group);

    auditService().createAuditEntry(ctx(), request());
    return YWResults.withData(group);
  }

  @ApiOperation(value = "deleteDefinitionGroup", response = YWResults.YWSuccess.class)
  public Result deleteDefinitionGroup(UUID customerUUID, UUID groupUUID) {
    Customer.getOrBadRequest(customerUUID);

    alertDefinitionGroupService.getOrBadRequest(groupUUID);

    alertDefinitionGroupService.delete(groupUUID);

    auditService().createAuditEntry(ctx(), request());
    return YWResults.YWSuccess.empty();
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

  @ApiOperation(value = "createAlertReceiver", response = AlertReceiver.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "CreateAlertReceiverRequest",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.AlertReceiverFormData",
          required = true))
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
    return YWResults.withData(receiver);
  }

  @ApiOperation(value = "getAlertReceiver", response = AlertReceiver.class)
  public Result getAlertReceiver(UUID customerUUID, UUID alertReceiverUUID) {
    Customer.getOrBadRequest(customerUUID);
    return YWResults.withData(AlertReceiver.getOrBadRequest(customerUUID, alertReceiverUUID));
  }

  @ApiOperation(value = "updateAlertReceiver", response = AlertReceiver.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "UpdateAlertReceiverRequest",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.AlertReceiverFormData",
          required = true))
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
    return YWResults.withData(receiver);
  }

  @ApiOperation(value = "deleteAlertReceiver", response = YWResults.YWSuccess.class)
  public Result deleteAlertReceiver(UUID customerUUID, UUID alertReceiverUUID) {
    Customer.getOrBadRequest(customerUUID);
    AlertReceiver receiver = AlertReceiver.getOrBadRequest(customerUUID, alertReceiverUUID);

    List<String> blockingRoutes =
        receiver
            .getRoutesList()
            .stream()
            .filter(route -> route.getReceiversList().size() == 1)
            .map(AlertRoute::getName)
            .sorted()
            .collect(Collectors.toList());
    if (!blockingRoutes.isEmpty()) {
      throw new YWServiceException(
          BAD_REQUEST,
          String.format(
              "Unable to delete alert receiver: %s. %d alert routes have it as a last receiver."
                  + " Examples: %s",
              alertReceiverUUID,
              blockingRoutes.size(),
              blockingRoutes.stream().limit(5).collect(Collectors.toList())));
    }

    log.info("Deleting alert receiver {} for customer {}", alertReceiverUUID, customerUUID);
    if (!receiver.delete()) {
      throw new YWServiceException(
          INTERNAL_SERVER_ERROR, "Unable to delete alert receiver: " + alertReceiverUUID);
    }

    auditService().createAuditEntry(ctx(), request());
    return YWResults.YWSuccess.empty();
  }

  @ApiOperation(
      value = "listAlertReceivers",
      response = AlertReceiver.class,
      responseContainer = "List")
  public Result listAlertReceivers(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);
    return YWResults.withData(AlertReceiver.list(customerUUID));
  }

  @ApiOperation(value = "createAlertRoute", response = AlertRoute.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "CreateAlertRouteRequest",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.AlertRouteFormData",
          required = true))
  public Result createAlertRoute(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);
    AlertRouteFormData data = formFactory.getFormDataOrBadRequest(AlertRouteFormData.class).get();
    List<AlertReceiver> receivers = AlertReceiver.getOrBadRequest(customerUUID, data.receivers);
    try {
      AlertRoute defaultRoute = AlertRoute.getDefaultRoute(customerUUID);
      AlertRoute route = AlertRoute.create(customerUUID, data.name, receivers, data.defaultRoute);
      if (data.defaultRoute && (defaultRoute != null)) {
        defaultRoute.setDefaultRoute(false);
        defaultRoute.save();
        log.info("For customer {} switched default route to {}", customerUUID, route.getUuid());
      }
      auditService().createAuditEntryWithReqBody(ctx());
      return YWResults.withData(route);
    } catch (Exception e) {
      throw new YWServiceException(BAD_REQUEST, "Unable to create alert route: " + e.getMessage());
    }
  }

  @ApiOperation(value = "getAlertRoute", response = AlertRoute.class)
  public Result getAlertRoute(UUID customerUUID, UUID alertRouteUUID) {
    Customer.getOrBadRequest(customerUUID);
    return YWResults.withData(AlertRoute.getOrBadRequest(customerUUID, alertRouteUUID));
  }

  @ApiOperation(value = "updateAlertRoute", response = AlertRoute.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "UpdateAlertRouteRequest",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.AlertRouteFormData",
          required = true))
  public Result updateAlertRoute(UUID customerUUID, UUID alertRouteUUID) {
    Customer.getOrBadRequest(customerUUID);
    AlertRouteFormData data = formFactory.getFormDataOrBadRequest(AlertRouteFormData.class).get();
    AlertRoute route = AlertRoute.getOrBadRequest(customerUUID, alertRouteUUID);

    if (route.isDefaultRoute() && !data.defaultRoute) {
      throw new YWServiceException(
          BAD_REQUEST,
          "Can't set the alert route as non-default. Make another route as default at first.");
    }

    List<AlertReceiver> receivers = AlertReceiver.getOrBadRequest(customerUUID, data.receivers);
    try {
      AlertRoute defaultRoute = AlertRoute.getDefaultRoute(customerUUID);
      route.setName(data.name);
      route.setReceiversList(receivers);
      route.setDefaultRoute(data.defaultRoute);
      route.save();
      if (data.defaultRoute
          && (defaultRoute != null)
          && !defaultRoute.getUuid().equals(alertRouteUUID)) {
        defaultRoute.setDefaultRoute(false);
        defaultRoute.save();
        log.info("For customer {} switched default route to {}", customerUUID, alertRouteUUID);
      }
      auditService().createAuditEntryWithReqBody(ctx());
      return YWResults.withData(route);
    } catch (Exception e) {
      throw new YWServiceException(BAD_REQUEST, "Unable to update alert route: " + e.getMessage());
    }
  }

  @ApiOperation(value = "deleteAlertRoute", response = YWResults.YWSuccess.class)
  public Result deleteAlertRoute(UUID customerUUID, UUID alertRouteUUID) {
    Customer.getOrBadRequest(customerUUID);
    AlertRoute route = AlertRoute.getOrBadRequest(customerUUID, alertRouteUUID);
    if (route.isDefaultRoute()) {
      throw new YWServiceException(
          BAD_REQUEST,
          String.format(
              "Unable to delete default alert route %s, make another route default at first.",
              alertRouteUUID));
    }

    log.info("Deleting alert route {} for customer {}", alertRouteUUID, customerUUID);

    AlertDefinitionGroupFilter groupFilter =
        AlertDefinitionGroupFilter.builder().routeUuid(route.getUuid()).build();
    List<AlertDefinitionGroup> groups = alertDefinitionGroupService.list(groupFilter);
    if (!groups.isEmpty()) {
      throw new YWServiceException(
          BAD_REQUEST,
          "Unable to delete alert route: "
              + alertRouteUUID
              + ". "
              + groups.size()
              + " alert definition groups are linked to it. Examples: "
              + groups
                  .stream()
                  .limit(5)
                  .map(AlertDefinitionGroup::getName)
                  .collect(Collectors.toList()));
    }
    if (!route.delete()) {
      throw new YWServiceException(
          INTERNAL_SERVER_ERROR, "Unable to delete alert route: " + alertRouteUUID);
    }

    auditService().createAuditEntry(ctx(), request());
    return YWResults.YWSuccess.empty();
  }

  @ApiOperation(value = "listAlertRoutes", response = AlertRoute.class, responseContainer = "List")
  public Result listAlertRoutes(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);
    return YWResults.withData(AlertRoute.listByCustomer(customerUUID));
  }
}
