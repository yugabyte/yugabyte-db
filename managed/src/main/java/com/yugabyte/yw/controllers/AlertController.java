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

import com.google.inject.Inject;
import com.yugabyte.yw.common.AlertManager;
import com.yugabyte.yw.common.AlertManager.SendNotificationResult;
import com.yugabyte.yw.common.AlertManager.SendNotificationStatus;
import com.yugabyte.yw.common.AlertTemplate;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.alerts.AlertChannelService;
import com.yugabyte.yw.common.alerts.AlertConfigurationService;
import com.yugabyte.yw.common.alerts.AlertDestinationService;
import com.yugabyte.yw.common.alerts.AlertService;
import com.yugabyte.yw.common.metrics.MetricService;
import com.yugabyte.yw.forms.AlertChannelFormData;
import com.yugabyte.yw.forms.AlertDestinationFormData;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.forms.filters.AlertApiFilter;
import com.yugabyte.yw.forms.filters.AlertConfigurationApiFilter;
import com.yugabyte.yw.forms.filters.AlertTemplateApiFilter;
import com.yugabyte.yw.forms.paging.AlertConfigurationPagedApiQuery;
import com.yugabyte.yw.forms.paging.AlertPagedApiQuery;
import com.yugabyte.yw.models.Alert;
import com.yugabyte.yw.models.AlertChannel;
import com.yugabyte.yw.models.AlertConfiguration;
import com.yugabyte.yw.models.AlertDefinition;
import com.yugabyte.yw.models.AlertDestination;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.extended.AlertConfigurationTemplate;
import com.yugabyte.yw.models.filters.AlertConfigurationFilter;
import com.yugabyte.yw.models.filters.AlertFilter;
import com.yugabyte.yw.models.filters.AlertTemplateFilter;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.paging.AlertConfigurationPagedQuery;
import com.yugabyte.yw.models.paging.AlertConfigurationPagedResponse;
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

@Api(value = "Alerts", authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class AlertController extends AuthenticatedController {

  @Inject private MetricService metricService;

  @Inject private AlertConfigurationService alertConfigurationService;

  @Inject private AlertService alertService;

  @Inject private AlertChannelService alertChannelService;

  @Inject private AlertDestinationService alertDestinationService;

  @Inject private AlertManager alertManager;

  @ApiOperation(value = "Get details of an alert", response = Alert.class)
  public Result get(UUID customerUUID, UUID alertUUID) {
    Customer.getOrBadRequest(customerUUID);

    Alert alert = alertService.getOrBadRequest(alertUUID);
    return PlatformResults.withData(alert);
  }

  /** Lists alerts for given customer. */
  @ApiOperation(
      value = "List all alerts",
      response = Alert.class,
      responseContainer = "List",
      nickname = "listOfAlerts")
  public Result list(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);

    AlertFilter filter = AlertFilter.builder().customerUuid(customerUUID).build();
    List<Alert> alerts = alertService.list(filter);
    return PlatformResults.withData(alerts);
  }

  @ApiOperation(value = "List active alerts", response = Alert.class, responseContainer = "List")
  public Result listActive(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);

    AlertFilter filter = AlertFilter.builder().customerUuid(customerUUID).build();
    List<Alert> alerts = alertService.listNotResolved(filter);
    return PlatformResults.withData(alerts);
  }

  @ApiOperation(value = "Count alerts", response = Integer.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "CountAlertsRequest",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.filters.AlertApiFilter",
          required = true))
  public Result countAlerts(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);

    AlertApiFilter apiFilter = parseJsonAndValidate(AlertApiFilter.class);

    AlertFilter filter = apiFilter.toFilter().toBuilder().customerUuid(customerUUID).build();

    int alertCount = alertService.count(filter);

    return PlatformResults.withData(alertCount);
  }

  @ApiOperation(value = "List alerts (paginated)", response = AlertPagedResponse.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "PageAlertsRequest",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.paging.AlertPagedApiQuery",
          required = true))
  public Result pageAlerts(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);

    AlertPagedApiQuery apiQuery = parseJsonAndValidate(AlertPagedApiQuery.class);
    AlertApiFilter apiFilter = apiQuery.getFilter();
    AlertFilter filter = apiFilter.toFilter().toBuilder().customerUuid(customerUUID).build();
    AlertPagedQuery query = apiQuery.copyWithFilter(filter, AlertPagedQuery.class);

    AlertPagedResponse alerts = alertService.pagedList(query);

    return PlatformResults.withData(alerts);
  }

  @ApiOperation(value = "Acknowledge an alert", response = Alert.class)
  public Result acknowledge(UUID customerUUID, UUID alertUUID) {
    Customer.getOrBadRequest(customerUUID);

    AlertFilter filter = AlertFilter.builder().uuid(alertUUID).build();
    alertService.acknowledge(filter);

    Alert alert = alertService.getOrBadRequest(alertUUID);
    return PlatformResults.withData(alert);
  }

  @ApiOperation(
      value = "Acknowledge all alerts",
      response = Alert.class,
      responseContainer = "List")
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "AcknowledgeAlertsRequest",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.filters.AlertApiFilter",
          required = true))
  public Result acknowledgeByFilter(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);

    AlertApiFilter apiFilter = parseJsonAndValidate(AlertApiFilter.class);
    AlertFilter filter = apiFilter.toFilter().toBuilder().customerUuid(customerUUID).build();

    alertService.acknowledge(filter);
    return YBPSuccess.empty();
  }

  @ApiOperation(value = "Get an alert configuration", response = AlertConfiguration.class)
  public Result getAlertConfiguration(UUID customerUUID, UUID configurationUUID) {
    Customer.getOrBadRequest(customerUUID);

    AlertConfiguration configuration = alertConfigurationService.getOrBadRequest(configurationUUID);

    return PlatformResults.withData(configuration);
  }

  @ApiOperation(
      value = "Get filtered list of alert configuration templates",
      response = AlertConfigurationTemplate.class,
      responseContainer = "List")
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "ListTemplatesRequest",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.filters.AlertTemplateApiFilter",
          required = true))
  public Result listAlertTemplates(UUID customerUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);

    AlertTemplateApiFilter apiFilter = parseJsonAndValidate(AlertTemplateApiFilter.class);
    AlertTemplateFilter filter = apiFilter.toFilter();

    List<AlertConfigurationTemplate> templates =
        Arrays.stream(AlertTemplate.values())
            .filter(filter::matches)
            .map(
                template ->
                    alertConfigurationService.createConfigurationTemplate(customer, template))
            .collect(Collectors.toList());

    return PlatformResults.withData(templates);
  }

  @ApiOperation(
      value = "List all alert configurations (paginated)",
      response = AlertConfigurationPagedResponse.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "PageAlertConfigurationsRequest",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.paging.AlertConfigurationPagedApiQuery",
          required = true))
  public Result pageAlertConfigurations(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);

    AlertConfigurationPagedApiQuery apiQuery =
        parseJsonAndValidate(AlertConfigurationPagedApiQuery.class);
    AlertConfigurationApiFilter apiFilter = apiQuery.getFilter();
    AlertConfigurationFilter filter =
        apiFilter.toFilter().toBuilder().customerUuid(customerUUID).build();
    AlertConfigurationPagedQuery query =
        apiQuery.copyWithFilter(filter, AlertConfigurationPagedQuery.class);

    AlertConfigurationPagedResponse configurations = alertConfigurationService.pagedList(query);

    return PlatformResults.withData(configurations);
  }

  @ApiOperation(
      value = "Get filtered list of alert configurations",
      response = AlertConfiguration.class,
      responseContainer = "List")
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "ListAlertConfigurationsRequest",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.filters.AlertConfigurationApiFilter",
          required = true))
  public Result listAlertConfigurations(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);

    AlertConfigurationApiFilter apiFilter = parseJsonAndValidate(AlertConfigurationApiFilter.class);
    AlertConfigurationFilter filter =
        apiFilter.toFilter().toBuilder().customerUuid(customerUUID).build();

    List<AlertConfiguration> configurations = alertConfigurationService.list(filter);

    return PlatformResults.withData(configurations);
  }

  @ApiOperation(value = "Create an alert configuration", response = AlertConfiguration.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "CreateAlertConfigurationRequest",
          paramType = "body",
          dataType = "com.yugabyte.yw.models.AlertConfiguration",
          required = true))
  public Result createAlertConfiguration(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);

    AlertConfiguration configuration = parseJson(AlertConfiguration.class);

    if (configuration.getUuid() != null) {
      throw new PlatformServiceException(BAD_REQUEST, "Can't create configuration with uuid set");
    }

    configuration = alertConfigurationService.save(configuration);

    auditService().createAuditEntry(ctx(), request());
    return PlatformResults.withData(configuration);
  }

  @ApiOperation(value = "Update an alert configuration", response = AlertConfiguration.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "UpdateAlertConfigurationRequest",
          paramType = "body",
          dataType = "com.yugabyte.yw.models.AlertConfiguration",
          required = true))
  public Result updateAlertConfiguration(UUID customerUUID, UUID configurationUUID) {
    Customer.getOrBadRequest(customerUUID);
    alertConfigurationService.getOrBadRequest(configurationUUID);

    AlertConfiguration configuration = parseJson(AlertConfiguration.class);

    if (configuration.getUuid() == null) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Can't update configuration with missing uuid");
    }

    if (!configuration.getUuid().equals(configurationUUID)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Configuration UUID from path should be consistent with body");
    }

    configuration = alertConfigurationService.save(configuration);

    auditService().createAuditEntry(ctx(), request());
    return PlatformResults.withData(configuration);
  }

  @ApiOperation(value = "Delete an alert configuration", response = YBPSuccess.class)
  public Result deleteAlertConfiguration(UUID customerUUID, UUID configurationUUID) {
    Customer.getOrBadRequest(customerUUID);

    alertConfigurationService.getOrBadRequest(configurationUUID);

    alertConfigurationService.delete(configurationUUID);

    auditService().createAuditEntry(ctx(), request());
    return YBPSuccess.empty();
  }

  @ApiOperation(value = "Send test alert for alert configuration", response = YBPSuccess.class)
  public Result sendTestAlert(UUID customerUUID, UUID configurationUUID) {
    Customer.getOrBadRequest(customerUUID);

    AlertConfiguration configuration = alertConfigurationService.getOrBadRequest(configurationUUID);
    Alert alert = alertConfigurationService.createTestAlert(configuration);
    SendNotificationResult result = alertManager.sendNotification(alert);
    if (result.getStatus() != SendNotificationStatus.SUCCEEDED) {
      throw new PlatformServiceException(BAD_REQUEST, result.getMessage());
    }
    return YBPSuccess.withMessage(result.getMessage());
  }

  @ApiOperation(value = "Create an alert channel", response = AlertChannel.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "CreateAlertChannelRequest",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.AlertChannelFormData",
          required = true))
  public Result createAlertChannel(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);
    AlertChannelFormData data = parseJsonAndValidate(AlertChannelFormData.class);
    AlertChannel channel =
        new AlertChannel().setCustomerUUID(customerUUID).setName(data.name).setParams(data.params);
    alertChannelService.save(channel);
    auditService().createAuditEntryWithReqBody(ctx());
    return PlatformResults.withData(channel);
  }

  @ApiOperation(value = "Get an alert channel", response = AlertChannel.class)
  public Result getAlertChannel(UUID customerUUID, UUID alertChannelUUID) {
    Customer.getOrBadRequest(customerUUID);
    return PlatformResults.withData(
        CommonUtils.maskObject(
            alertChannelService.getOrBadRequest(customerUUID, alertChannelUUID)));
  }

  @ApiOperation(value = "Update an alert channel", response = AlertChannel.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "UpdateAlertChannelRequest",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.AlertChannelFormData",
          required = true))
  public Result updateAlertChannel(UUID customerUUID, UUID alertChannelUUID) {
    Customer.getOrBadRequest(customerUUID);
    AlertChannel channel = alertChannelService.getOrBadRequest(customerUUID, alertChannelUUID);
    AlertChannelFormData data = parseJsonAndValidate(AlertChannelFormData.class);
    channel
        .setName(data.name)
        .setParams(CommonUtils.unmaskObject(channel.getParams(), data.params));
    alertChannelService.save(channel);
    auditService().createAuditEntryWithReqBody(ctx());
    return PlatformResults.withData(CommonUtils.maskObject(channel));
  }

  @ApiOperation(value = "Delete an alert channel", response = YBPSuccess.class)
  public Result deleteAlertChannel(UUID customerUUID, UUID alertChannelUUID) {
    Customer.getOrBadRequest(customerUUID);
    AlertChannel channel = alertChannelService.getOrBadRequest(customerUUID, alertChannelUUID);
    alertChannelService.delete(customerUUID, alertChannelUUID);
    metricService.handleSourceRemoval(channel.getCustomerUUID(), alertChannelUUID);
    auditService().createAuditEntry(ctx(), request());
    return YBPSuccess.empty();
  }

  @ApiOperation(
      value = "List all alert channels",
      response = AlertChannel.class,
      responseContainer = "List")
  public Result listAlertChannels(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);
    return PlatformResults.withData(
        alertChannelService
            .list(customerUUID)
            .stream()
            .map(channel -> CommonUtils.maskObject(channel))
            .collect(Collectors.toList()));
  }

  @ApiOperation(value = "Create an alert destination", response = AlertDestination.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "CreateAlertDestinationRequest",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.AlertDestinationFormData",
          required = true))
  public Result createAlertDestination(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);
    AlertDestinationFormData data =
        formFactory.getFormDataOrBadRequest(AlertDestinationFormData.class).get();
    AlertDestination destination =
        new AlertDestination()
            .setCustomerUUID(customerUUID)
            .setName(data.name)
            .setChannelsList(alertChannelService.getOrBadRequest(customerUUID, data.channels))
            .setDefaultDestination(data.defaultDestination);
    alertDestinationService.save(destination);
    auditService().createAuditEntryWithReqBody(ctx());
    return PlatformResults.withData(destination);
  }

  @ApiOperation(value = "Get an alert destination", response = AlertDestination.class)
  public Result getAlertDestination(UUID customerUUID, UUID alertDestinationUUID) {
    Customer.getOrBadRequest(customerUUID);
    return PlatformResults.withData(
        alertDestinationService.getOrBadRequest(customerUUID, alertDestinationUUID));
  }

  @ApiOperation(value = "Update an alert destination", response = AlertDestination.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "UpdateAlertDestinationRequest",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.AlertDestinationFormData",
          required = true))
  public Result updateAlertDestination(UUID customerUUID, UUID alertDestinationUUID) {
    Customer.getOrBadRequest(customerUUID);
    AlertDestinationFormData data =
        formFactory.getFormDataOrBadRequest(AlertDestinationFormData.class).get();
    AlertDestination destination =
        alertDestinationService.getOrBadRequest(customerUUID, alertDestinationUUID);
    destination
        .setName(data.name)
        .setDefaultDestination(data.defaultDestination)
        .setChannelsList(alertChannelService.getOrBadRequest(customerUUID, data.channels));
    alertDestinationService.save(destination);
    auditService().createAuditEntryWithReqBody(ctx());
    return PlatformResults.withData(destination);
  }

  @ApiOperation(value = "Delete an alert destination", response = YBPSuccess.class)
  public Result deleteAlertDestination(UUID customerUUID, UUID alertDestinationUUID) {
    Customer.getOrBadRequest(customerUUID);
    alertDestinationService.delete(customerUUID, alertDestinationUUID);
    auditService().createAuditEntry(ctx(), request());
    return YBPSuccess.empty();
  }

  @ApiOperation(
      value = "List alert destinations",
      response = AlertDefinition.class,
      responseContainer = "List")
  public Result listAlertDestinations(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);
    return PlatformResults.withData(alertDestinationService.listByCustomer(customerUUID));
  }
}
