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
import com.yugabyte.yw.common.alerts.AlertDefinitionService;
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
import com.yugabyte.yw.metrics.MetricUrlProvider;
import com.yugabyte.yw.models.Alert;
import com.yugabyte.yw.models.AlertChannel;
import com.yugabyte.yw.models.AlertConfiguration;
import com.yugabyte.yw.models.AlertDefinition;
import com.yugabyte.yw.models.AlertDestination;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.extended.AlertConfigurationTemplate;
import com.yugabyte.yw.models.extended.AlertData;
import com.yugabyte.yw.models.filters.AlertConfigurationFilter;
import com.yugabyte.yw.models.filters.AlertDefinitionFilter;
import com.yugabyte.yw.models.filters.AlertFilter;
import com.yugabyte.yw.models.filters.AlertTemplateFilter;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import com.yugabyte.yw.models.paging.AlertConfigurationPagedQuery;
import com.yugabyte.yw.models.paging.AlertConfigurationPagedResponse;
import com.yugabyte.yw.models.paging.AlertDataPagedResponse;
import com.yugabyte.yw.models.paging.AlertPagedQuery;
import com.yugabyte.yw.models.paging.AlertPagedResponse;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiImplicitParam;
import io.swagger.annotations.ApiImplicitParams;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.UUID;
import java.util.concurrent.TimeUnit;
import java.util.function.Function;
import java.util.stream.Collectors;
import org.apache.commons.collections4.CollectionUtils;
import play.mvc.Result;

@Api(value = "Alerts", authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class AlertController extends AuthenticatedController {

  @Inject private MetricService metricService;

  @Inject private AlertConfigurationService alertConfigurationService;

  @Inject private AlertDefinitionService alertDefinitionService;

  @Inject private AlertService alertService;

  @Inject private AlertChannelService alertChannelService;

  @Inject private AlertDestinationService alertDestinationService;

  @Inject private AlertManager alertManager;

  @Inject private MetricUrlProvider metricUrlProvider;

  @ApiOperation(value = "Get details of an alert", response = Alert.class)
  public Result get(UUID customerUUID, UUID alertUUID) {
    Customer.getOrBadRequest(customerUUID);

    Alert alert = alertService.getOrBadRequest(alertUUID);
    return PlatformResults.withData(convert(alert));
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

    return PlatformResults.withData(convert(alerts));
  }

  @ApiOperation(value = "List active alerts", response = Alert.class, responseContainer = "List")
  public Result listActive(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);

    AlertFilter filter = AlertFilter.builder().customerUuid(customerUUID).build();
    List<Alert> alerts = alertService.listNotResolved(filter);

    return PlatformResults.withData(convert(alerts));
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
    List<AlertData> alertDataList = convert(alerts.getEntities());
    AlertDataPagedResponse alertDataPagedResponse =
        alerts.setData(alertDataList, new AlertDataPagedResponse());

    return PlatformResults.withData(alertDataPagedResponse);
  }

  @ApiOperation(value = "Acknowledge an alert", response = Alert.class)
  public Result acknowledge(UUID customerUUID, UUID alertUUID) {
    Customer.getOrBadRequest(customerUUID);

    AlertFilter filter = AlertFilter.builder().uuid(alertUUID).build();
    alertService.acknowledge(filter);

    Alert alert = alertService.getOrBadRequest(alertUUID);
    auditService()
        .createAuditEntryWithReqBody(
            ctx(), Audit.TargetType.Alert, alertUUID.toString(), Audit.ActionType.Acknowledge);
    return PlatformResults.withData(convert(alert));
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

    String alertUUIDs = "";
    for (UUID uuid : filter.getUuids()) {
      alertUUIDs += uuid.toString() + " ";
    }
    auditService()
        .createAuditEntryWithReqBody(
            ctx(), Audit.TargetType.Alert, alertUUIDs, Audit.ActionType.Acknowledge);
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

    auditService()
        .createAuditEntryWithReqBody(
            ctx(),
            Audit.TargetType.Alert,
            Objects.toString(configuration.getUuid(), null),
            Audit.ActionType.Create,
            request().body().asJson());
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

    auditService()
        .createAuditEntryWithReqBody(
            ctx(),
            Audit.TargetType.Alert,
            configurationUUID.toString(),
            Audit.ActionType.Update,
            request().body().asJson());
    return PlatformResults.withData(configuration);
  }

  @ApiOperation(value = "Delete an alert configuration", response = YBPSuccess.class)
  public Result deleteAlertConfiguration(UUID customerUUID, UUID configurationUUID) {
    Customer.getOrBadRequest(customerUUID);

    alertConfigurationService.getOrBadRequest(configurationUUID);

    alertConfigurationService.delete(configurationUUID);

    auditService()
        .createAuditEntryWithReqBody(
            ctx(),
            Audit.TargetType.Alert,
            configurationUUID.toString(),
            Audit.ActionType.Delete,
            request().body().asJson());
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
    auditService()
        .createAuditEntryWithReqBody(
            ctx(),
            Audit.TargetType.AlertChannel,
            Objects.toString(channel.getUuid(), null),
            Audit.ActionType.Create,
            request().body().asJson());
    return PlatformResults.withData(CommonUtils.maskObject(channel));
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
    auditService()
        .createAuditEntryWithReqBody(
            ctx(),
            Audit.TargetType.AlertChannel,
            alertChannelUUID.toString(),
            Audit.ActionType.Update,
            request().body().asJson());
    return PlatformResults.withData(CommonUtils.maskObject(channel));
  }

  @ApiOperation(value = "Delete an alert channel", response = YBPSuccess.class)
  public Result deleteAlertChannel(UUID customerUUID, UUID alertChannelUUID) {
    Customer.getOrBadRequest(customerUUID);
    AlertChannel channel = alertChannelService.getOrBadRequest(customerUUID, alertChannelUUID);
    alertChannelService.delete(customerUUID, alertChannelUUID);
    metricService.markSourceRemoved(channel.getCustomerUUID(), alertChannelUUID);
    auditService()
        .createAuditEntryWithReqBody(
            ctx(),
            Audit.TargetType.AlertChannel,
            alertChannelUUID.toString(),
            Audit.ActionType.Delete);
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
    auditService()
        .createAuditEntryWithReqBody(
            ctx(),
            Audit.TargetType.AlertDestination,
            Objects.toString(destination.getUuid(), null),
            Audit.ActionType.Create,
            request().body().asJson());
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
    auditService()
        .createAuditEntryWithReqBody(
            ctx(),
            Audit.TargetType.AlertDestination,
            alertDestinationUUID.toString(),
            Audit.ActionType.Update,
            request().body().asJson());
    return PlatformResults.withData(destination);
  }

  @ApiOperation(value = "Delete an alert destination", response = YBPSuccess.class)
  public Result deleteAlertDestination(UUID customerUUID, UUID alertDestinationUUID) {
    Customer.getOrBadRequest(customerUUID);
    alertDestinationService.delete(customerUUID, alertDestinationUUID);
    auditService()
        .createAuditEntryWithReqBody(
            ctx(),
            Audit.TargetType.AlertDestination,
            alertDestinationUUID.toString(),
            Audit.ActionType.Delete);
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

  private AlertData convert(Alert alert) {
    return convert(Collections.singletonList(alert)).get(0);
  }

  private List<AlertData> convert(List<Alert> alerts) {
    List<Alert> alertsWithoutExpressionLabel =
        alerts
            .stream()
            .filter(alert -> alert.getLabelValue(KnownAlertLabels.ALERT_EXPRESSION) == null)
            .collect(Collectors.toList());
    List<UUID> alertsConfigurationUuids =
        alertsWithoutExpressionLabel
            .stream()
            .map(Alert::getConfigurationUuid)
            .collect(Collectors.toList());
    List<UUID> alertDefinitionUuids =
        alertsWithoutExpressionLabel
            .stream()
            .map(Alert::getDefinitionUuid)
            .collect(Collectors.toList());
    Map<UUID, AlertConfiguration> alertConfigurationMap =
        CollectionUtils.isNotEmpty(alertsConfigurationUuids)
            ? alertConfigurationService
                .list(AlertConfigurationFilter.builder().uuids(alertsConfigurationUuids).build())
                .stream()
                .collect(Collectors.toMap(AlertConfiguration::getUuid, Function.identity()))
            : Collections.emptyMap();
    Map<UUID, AlertDefinition> alertDefinitionMap =
        CollectionUtils.isNotEmpty(alertDefinitionUuids)
            ? alertDefinitionService
                .list(AlertDefinitionFilter.builder().uuids(alertDefinitionUuids).build())
                .stream()
                .collect(Collectors.toMap(AlertDefinition::getUuid, Function.identity()))
            : Collections.emptyMap();
    return alerts
        .stream()
        .map(
            alert ->
                convertInternal(
                    alert,
                    alertConfigurationMap.get(alert.getConfigurationUuid()),
                    alertDefinitionMap.get(alert.getDefinitionUuid())))
        .collect(Collectors.toList());
  }

  private AlertData convertInternal(
      Alert alert, AlertConfiguration alertConfiguration, AlertDefinition alertDefinition) {
    return new AlertData()
        .setAlert(alert)
        .setAlertExpressionUrl(getAlertExpressionUrl(alert, alertConfiguration, alertDefinition));
  }

  private String getAlertExpressionUrl(
      Alert alert, AlertConfiguration alertConfiguration, AlertDefinition alertDefinition) {
    String expression = alert.getLabelValue(KnownAlertLabels.ALERT_EXPRESSION);
    if (expression == null) {
      // Old resolved alerts may not have alert expression.
      // Will try to get from configuration and definition.
      // Thresholds and even expression itself may already be changed - but that's best we can do.
      if (alertConfiguration != null && alertDefinition != null) {
        expression =
            alertDefinition.getQueryWithThreshold(
                alertConfiguration.getThresholds().get(alert.getSeverity()));
      } else {
        return null;
      }
    }
    Long startUnixTime = TimeUnit.MILLISECONDS.toSeconds(alert.getCreateTime().getTime());
    Long endUnixTime =
        TimeUnit.MILLISECONDS.toSeconds(
            alert.getResolvedTime() != null
                ? alert.getResolvedTime().getTime()
                : System.currentTimeMillis());
    return metricUrlProvider.getExpressionUrl(expression, startUnixTime, endUnixTime);
  }
}
