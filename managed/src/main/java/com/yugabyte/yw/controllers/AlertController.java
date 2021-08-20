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

import com.google.common.annotations.VisibleForTesting;
import com.google.inject.Inject;
import com.yugabyte.yw.common.AlertTemplate;
import com.yugabyte.yw.common.YWServiceException;
import com.yugabyte.yw.common.alerts.AlertDefinitionGroupService;
import com.yugabyte.yw.common.alerts.AlertReceiverService;
import com.yugabyte.yw.common.alerts.AlertRouteService;
import com.yugabyte.yw.common.alerts.AlertService;
import com.yugabyte.yw.common.metrics.MetricService;
import com.yugabyte.yw.common.alerts.impl.AlertDefinitionTemplate;
import com.yugabyte.yw.forms.AlertReceiverFormData;
import com.yugabyte.yw.forms.AlertRouteFormData;
import com.yugabyte.yw.forms.YWResults;
import com.yugabyte.yw.forms.filters.AlertApiFilter;
import com.yugabyte.yw.forms.filters.AlertDefinitionGroupApiFilter;
import com.yugabyte.yw.forms.filters.AlertDefinitionTemplateApiFilter;
import com.yugabyte.yw.forms.paging.AlertDefinitionGroupPagedApiQuery;
import com.yugabyte.yw.forms.paging.AlertPagedApiQuery;
import com.yugabyte.yw.models.Alert;
import com.yugabyte.yw.models.AlertDefinitionGroup;
import com.yugabyte.yw.models.AlertReceiver;
import com.yugabyte.yw.models.AlertRoute;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.filters.AlertDefinitionGroupFilter;
import com.yugabyte.yw.models.filters.AlertDefinitionTemplateFilter;
import com.yugabyte.yw.models.filters.AlertFilter;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.paging.AlertDefinitionGroupPagedQuery;
import com.yugabyte.yw.models.paging.AlertDefinitionGroupPagedResponse;
import com.yugabyte.yw.models.paging.AlertPagedQuery;
import com.yugabyte.yw.models.paging.AlertPagedResponse;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiImplicitParam;
import io.swagger.annotations.ApiImplicitParams;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import java.util.Arrays;
import java.util.List;
import java.util.UUID;
import java.util.stream.Collectors;
import play.mvc.Result;

@Api(value = "Alert", authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class AlertController extends AuthenticatedController {

  @Inject private MetricService metricService;

  @Inject private AlertDefinitionGroupService alertDefinitionGroupService;

  @Inject private AlertService alertService;

  @Inject private AlertReceiverService alertReceiverService;

  @Inject private AlertRouteService alertRouteService;

  @ApiOperation(value = "getAlert", response = Alert.class)
  public Result get(UUID customerUUID, UUID alertUUID) {
    Customer.getOrBadRequest(customerUUID);

    Alert alert = alertService.getOrBadRequest(alertUUID);
    return YWResults.withData(alert);
  }

  /** Lists alerts for given customer. */
  @ApiOperation(
      value = "listAlerts",
      response = Alert.class,
      responseContainer = "List",
      nickname = "listOfAlerts")
  public Result list(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);

    AlertFilter filter = AlertFilter.builder().customerUuid(customerUUID).build();
    List<Alert> alerts = alertService.list(filter);
    return YWResults.withData(alerts);
  }

  @ApiOperation(value = "listActiveAlerts", response = Alert.class, responseContainer = "List")
  public Result listActive(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);

    AlertFilter filter = AlertFilter.builder().customerUuid(customerUUID).build();
    List<Alert> alerts = alertService.listNotResolved(filter);
    return YWResults.withData(alerts);
  }

  @ApiOperation(value = "pageAlerts", response = AlertPagedResponse.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "PageAlertsRequest",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.paging.AlertPagedApiQuery",
          required = true))
  public Result pageAlerts(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);

    AlertPagedApiQuery apiQuery = parseJson(AlertPagedApiQuery.class);
    AlertApiFilter apiFilter = apiQuery.getFilter();
    AlertFilter filter = apiFilter.toFilter().toBuilder().customerUuid(customerUUID).build();
    AlertPagedQuery query = apiQuery.copyWithFilter(filter, AlertPagedQuery.class);

    AlertPagedResponse alerts = alertService.pagedList(query);

    return YWResults.withData(alerts);
  }

  @ApiOperation(value = "acknowledgeAlert", response = Alert.class)
  public Result acknowledge(UUID customerUUID, UUID alertUUID) {
    Customer.getOrBadRequest(customerUUID);

    AlertFilter filter = AlertFilter.builder().uuid(alertUUID).build();
    alertService.acknowledge(filter);

    Alert alert = alertService.getOrBadRequest(alertUUID);
    return YWResults.withData(alert);
  }

  @ApiOperation(value = "acknowledgeAlerts", response = Alert.class, responseContainer = "List")
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "AcknowledgeAlertsRequest",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.filters.AlertApiFilter",
          required = true))
  public Result acknowledgeByFilter(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);

    AlertApiFilter apiFilter = parseJson(AlertApiFilter.class);
    AlertFilter filter = apiFilter.toFilter().toBuilder().customerUuid(customerUUID).build();

    alertService.acknowledge(filter);
    return YWResults.YWSuccess.empty();
  }

  @ApiOperation(value = "getDefinitionGroup", response = AlertDefinitionGroup.class)
  public Result getDefinitionGroup(UUID customerUUID, UUID groupUUID) {
    Customer.getOrBadRequest(customerUUID);

    AlertDefinitionGroup group = alertDefinitionGroupService.getOrBadRequest(groupUUID);

    return YWResults.withData(group);
  }

  @ApiOperation(
      value = "listDefinitionGroupTemplates",
      response = AlertDefinitionTemplate.class,
      responseContainer = "List")
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "ListTemplatesRequest",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.filters.AlertDefinitionTemplateApiFilter",
          required = true))
  public Result listDefinitionGroupTemplates(UUID customerUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);

    AlertDefinitionTemplateApiFilter apiFilter = parseJson(AlertDefinitionTemplateApiFilter.class);
    AlertDefinitionTemplateFilter filter = apiFilter.toFilter();

    List<AlertDefinitionTemplate> groups =
        Arrays.stream(AlertTemplate.values())
            .filter(filter::matches)
            .map(
                template ->
                    alertDefinitionGroupService.createDefinitionTemplate(customer, template))
            .collect(Collectors.toList());

    return YWResults.withData(groups);
  }

  @ApiOperation(value = "pageDefinitionGroups", response = AlertDefinitionGroupPagedResponse.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "PageDefinitionGroupsRequest",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.paging.AlertDefinitionGroupPagedApiQuery",
          required = true))
  public Result pageDefinitionGroups(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);

    AlertDefinitionGroupPagedApiQuery apiQuery = parseJson(AlertDefinitionGroupPagedApiQuery.class);
    AlertDefinitionGroupApiFilter apiFilter = apiQuery.getFilter();
    AlertDefinitionGroupFilter filter =
        apiFilter.toFilter().toBuilder().customerUuid(customerUUID).build();
    AlertDefinitionGroupPagedQuery query =
        apiQuery.copyWithFilter(filter, AlertDefinitionGroupPagedQuery.class);

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
          dataType = "com.yugabyte.yw.forms.filters.AlertDefinitionGroupApiFilter",
          required = true))
  public Result listDefinitionGroups(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);

    AlertDefinitionGroupApiFilter apiFilter = parseJson(AlertDefinitionGroupApiFilter.class);
    AlertDefinitionGroupFilter filter =
        apiFilter.toFilter().toBuilder().customerUuid(customerUUID).build();

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

    AlertDefinitionGroup group = parseJson(AlertDefinitionGroup.class);

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

    AlertDefinitionGroup group = parseJson(AlertDefinitionGroup.class);

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

  @ApiOperation(value = "createAlertReceiver", response = AlertReceiver.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "CreateAlertReceiverRequest",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.AlertReceiverFormData",
          required = true))
  public Result createAlertReceiver(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);
    AlertReceiverFormData data = parseJson(AlertReceiverFormData.class);
    AlertReceiver receiver =
        new AlertReceiver().setCustomerUUID(customerUUID).setName(data.name).setParams(data.params);
    alertReceiverService.save(receiver);
    auditService().createAuditEntryWithReqBody(ctx());
    return YWResults.withData(receiver);
  }

  @ApiOperation(value = "getAlertReceiver", response = AlertReceiver.class)
  public Result getAlertReceiver(UUID customerUUID, UUID alertReceiverUUID) {
    Customer.getOrBadRequest(customerUUID);
    return YWResults.withData(
        CommonUtils.maskObject(
            alertReceiverService.getOrBadRequest(customerUUID, alertReceiverUUID)));
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
    AlertReceiver receiver = alertReceiverService.getOrBadRequest(customerUUID, alertReceiverUUID);
    AlertReceiverFormData data = parseJson(AlertReceiverFormData.class);
    receiver
        .setName(data.name)
        .setParams(CommonUtils.unmaskObject(receiver.getParams(), data.params));
    alertReceiverService.save(receiver);
    auditService().createAuditEntryWithReqBody(ctx());
    return YWResults.withData(CommonUtils.maskObject(receiver));
  }

  @ApiOperation(value = "deleteAlertReceiver", response = YWResults.YWSuccess.class)
  public Result deleteAlertReceiver(UUID customerUUID, UUID alertReceiverUUID) {
    Customer.getOrBadRequest(customerUUID);
    AlertReceiver receiver = alertReceiverService.getOrBadRequest(customerUUID, alertReceiverUUID);
    alertReceiverService.delete(customerUUID, alertReceiverUUID);
    metricService.handleTargetRemoval(receiver.getCustomerUUID(), alertReceiverUUID);
    auditService().createAuditEntry(ctx(), request());
    return YWResults.YWSuccess.empty();
  }

  @ApiOperation(
      value = "listAlertReceivers",
      response = AlertReceiver.class,
      responseContainer = "List")
  public Result listAlertReceivers(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);
    return YWResults.withData(
        alertReceiverService
            .list(customerUUID)
            .stream()
            .map(receiver -> CommonUtils.maskObject(receiver))
            .collect(Collectors.toList()));
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
    AlertRoute route =
        new AlertRoute()
            .setCustomerUUID(customerUUID)
            .setName(data.name)
            .setReceiversList(alertReceiverService.getOrBadRequest(customerUUID, data.receivers))
            .setDefaultRoute(data.defaultRoute);
    alertRouteService.save(route);
    auditService().createAuditEntryWithReqBody(ctx());
    return YWResults.withData(route);
  }

  @ApiOperation(value = "getAlertRoute", response = AlertRoute.class)
  public Result getAlertRoute(UUID customerUUID, UUID alertRouteUUID) {
    Customer.getOrBadRequest(customerUUID);
    return YWResults.withData(alertRouteService.getOrBadRequest(customerUUID, alertRouteUUID));
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
    AlertRoute route = alertRouteService.getOrBadRequest(customerUUID, alertRouteUUID);
    route
        .setName(data.name)
        .setDefaultRoute(data.defaultRoute)
        .setReceiversList(alertReceiverService.getOrBadRequest(customerUUID, data.receivers));
    alertRouteService.save(route);
    auditService().createAuditEntryWithReqBody(ctx());
    return YWResults.withData(route);
  }

  @ApiOperation(value = "deleteAlertRoute", response = YWResults.YWSuccess.class)
  public Result deleteAlertRoute(UUID customerUUID, UUID alertRouteUUID) {
    Customer.getOrBadRequest(customerUUID);
    alertRouteService.delete(customerUUID, alertRouteUUID);
    auditService().createAuditEntry(ctx(), request());
    return YWResults.YWSuccess.empty();
  }

  @ApiOperation(value = "listAlertRoutes", response = AlertRoute.class, responseContainer = "List")
  public Result listAlertRoutes(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);
    return YWResults.withData(alertRouteService.listByCustomer(customerUUID));
  }

  @VisibleForTesting
  void setAlertDefinitionGroupService(AlertDefinitionGroupService alertDefinitionGroupService) {
    this.alertDefinitionGroupService = alertDefinitionGroupService;
  }

  @VisibleForTesting
  void setAlertService(AlertService alertService) {
    this.alertService = alertService;
  }

  @VisibleForTesting
  void setMetricService(MetricService metricService) {
    this.metricService = metricService;
  }
}
