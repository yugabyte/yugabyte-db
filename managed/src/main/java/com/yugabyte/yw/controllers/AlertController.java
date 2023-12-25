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

import static com.yugabyte.yw.common.alerts.AlertRuleTemplateSubstitutor.*;

import com.cronutils.utils.VisibleForTesting;
import com.google.inject.Inject;
import com.yugabyte.yw.common.AlertManager;
import com.yugabyte.yw.common.AlertManager.SendNotificationResult;
import com.yugabyte.yw.common.AlertManager.SendNotificationStatus;
import com.yugabyte.yw.common.AlertTemplate;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.alerts.*;
import com.yugabyte.yw.common.alerts.impl.AlertChannelBase;
import com.yugabyte.yw.common.alerts.impl.AlertChannelBase.Context;
import com.yugabyte.yw.common.alerts.impl.AlertTemplateService;
import com.yugabyte.yw.common.alerts.impl.AlertTemplateService.AlertTemplateDescription;
import com.yugabyte.yw.common.alerts.impl.AlertTemplateService.TestAlertSettings;
import com.yugabyte.yw.common.metrics.MetricLabelsBuilder;
import com.yugabyte.yw.common.metrics.MetricService;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.forms.*;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.forms.filters.AlertApiFilter;
import com.yugabyte.yw.forms.filters.AlertConfigurationApiFilter;
import com.yugabyte.yw.forms.filters.AlertTemplateApiFilter;
import com.yugabyte.yw.forms.paging.AlertConfigurationPagedApiQuery;
import com.yugabyte.yw.forms.paging.AlertPagedApiQuery;
import com.yugabyte.yw.metrics.MetricUrlProvider;
import com.yugabyte.yw.models.*;
import com.yugabyte.yw.models.AlertChannel.ChannelType;
import com.yugabyte.yw.models.AlertConfiguration.Severity;
import com.yugabyte.yw.models.Audit.ActionType;
import com.yugabyte.yw.models.Audit.TargetType;
import com.yugabyte.yw.models.extended.AlertConfigurationTemplate;
import com.yugabyte.yw.models.extended.AlertData;
import com.yugabyte.yw.models.filters.*;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import com.yugabyte.yw.models.paging.*;
import com.yugabyte.yw.rbac.annotations.AuthzPath;
import com.yugabyte.yw.rbac.annotations.PermissionAttribute;
import com.yugabyte.yw.rbac.annotations.RequiredPermissionOnResource;
import com.yugabyte.yw.rbac.annotations.Resource;
import com.yugabyte.yw.rbac.enums.SourceType;
import io.swagger.annotations.*;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiImplicitParam;
import io.swagger.annotations.ApiImplicitParams;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import java.util.*;
import java.util.concurrent.TimeUnit;
import java.util.function.Function;
import java.util.stream.Collectors;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.lang3.StringUtils;
import play.mvc.Http;
import play.mvc.Http.Request;
import play.mvc.Result;

@Api(value = "Alerts", authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class AlertController extends AuthenticatedController {

  @Inject private MetricService metricService;

  @Inject private AlertTemplateService alertTemplateService;

  @Inject private AlertConfigurationService alertConfigurationService;

  @Inject private AlertDefinitionService alertDefinitionService;

  @Inject private AlertService alertService;

  @Inject private AlertChannelService alertChannelService;

  @Inject private AlertChannelTemplateService alertChannelTemplateService;

  @Inject private AlertDestinationService alertDestinationService;

  @Inject private AlertManager alertManager;

  @Inject private MetricUrlProvider metricUrlProvider;

  @Inject private AlertTemplateSettingsService alertTemplateSettingsService;

  @Inject private AlertTemplateVariableService alertTemplateVariableService;

  @ApiOperation(value = "Get details of an alert", response = Alert.class)
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result get(UUID customerUUID, UUID alertUUID) {
    Customer.getOrBadRequest(customerUUID);

    Alert alert = alertService.getOrBadRequest(customerUUID, alertUUID);
    return PlatformResults.withData(convert(alert));
  }

  /** Lists alerts for given customer. */
  @ApiOperation(
      value = "List all alerts",
      response = Alert.class,
      responseContainer = "List",
      nickname = "listOfAlerts")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result list(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);

    AlertFilter filter = AlertFilter.builder().customerUuid(customerUUID).build();
    List<Alert> alerts = alertService.list(filter);

    return PlatformResults.withData(convert(alerts));
  }

  @ApiOperation(value = "List active alerts", response = Alert.class, responseContainer = "List")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
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
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result countAlerts(UUID customerUUID, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);

    AlertApiFilter apiFilter = parseJsonAndValidate(request, AlertApiFilter.class);

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
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result pageAlerts(UUID customerUUID, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);

    AlertPagedApiQuery apiQuery = parseJsonAndValidate(request, AlertPagedApiQuery.class);
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
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result acknowledge(UUID customerUUID, UUID alertUUID, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);

    AlertFilter filter = AlertFilter.builder().uuid(alertUUID).build();
    alertService.acknowledge(filter);

    Alert alert = alertService.getOrBadRequest(customerUUID, alertUUID);
    auditService()
        .createAuditEntry(
            request, Audit.TargetType.Alert, alertUUID.toString(), Audit.ActionType.Acknowledge);
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
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result acknowledgeByFilter(UUID customerUUID, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);

    AlertApiFilter apiFilter = parseJsonAndValidate(request, AlertApiFilter.class);
    AlertFilter filter = apiFilter.toFilter().toBuilder().customerUuid(customerUUID).build();

    alertService.acknowledge(filter);

    String alertUUIDs = "";
    for (UUID uuid : filter.getUuids()) {
      alertUUIDs += uuid.toString() + " ";
    }
    auditService()
        .createAuditEntryWithReqBody(
            request, Audit.TargetType.Alert, alertUUIDs, Audit.ActionType.Acknowledge);
    return YBPSuccess.empty();
  }

  @ApiOperation(value = "Get an alert configuration", response = AlertConfiguration.class)
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result getAlertConfiguration(UUID customerUUID, UUID configurationUUID) {
    Customer.getOrBadRequest(customerUUID);

    AlertConfiguration configuration =
        alertConfigurationService.getOrBadRequest(customerUUID, configurationUUID);

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
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result listAlertTemplates(UUID customerUUID, Http.Request request) {
    Customer customer = Customer.getOrBadRequest(customerUUID);

    AlertTemplateApiFilter apiFilter = parseJsonAndValidate(request, AlertTemplateApiFilter.class);
    AlertTemplateFilter filter = apiFilter.toFilter();

    List<AlertConfigurationTemplate> templates =
        Arrays.stream(AlertTemplate.values())
            .filter(
                template -> filter.matches(alertTemplateService.getTemplateDescription(template)))
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
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result pageAlertConfigurations(UUID customerUUID, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);

    AlertConfigurationPagedApiQuery apiQuery =
        parseJsonAndValidate(request, AlertConfigurationPagedApiQuery.class);
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
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result listAlertConfigurations(UUID customerUUID, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);

    AlertConfigurationApiFilter apiFilter =
        parseJsonAndValidate(request, AlertConfigurationApiFilter.class);
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
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.CREATE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result createAlertConfiguration(UUID customerUUID, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);

    AlertConfiguration configuration = parseJson(request, AlertConfiguration.class);

    if (configuration.getUuid() != null) {
      throw new PlatformServiceException(BAD_REQUEST, "Can't create configuration with uuid set");
    }

    configuration = alertConfigurationService.save(configuration);

    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.Alert,
            Objects.toString(configuration.getUuid(), null),
            Audit.ActionType.Create);
    return PlatformResults.withData(configuration);
  }

  @ApiOperation(value = "Update an alert configuration", response = AlertConfiguration.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "UpdateAlertConfigurationRequest",
          paramType = "body",
          dataType = "com.yugabyte.yw.models.AlertConfiguration",
          required = true))
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result updateAlertConfiguration(
      UUID customerUUID, UUID configurationUUID, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);
    alertConfigurationService.getOrBadRequest(customerUUID, configurationUUID);

    AlertConfiguration configuration = parseJson(request, AlertConfiguration.class);

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
            request, Audit.TargetType.Alert, configurationUUID.toString(), Audit.ActionType.Update);
    return PlatformResults.withData(configuration);
  }

  @ApiOperation(value = "Delete an alert configuration", response = YBPSuccess.class)
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.DELETE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result deleteAlertConfiguration(
      UUID customerUUID, UUID configurationUUID, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);

    alertConfigurationService.getOrBadRequest(customerUUID, configurationUUID);

    alertConfigurationService.delete(configurationUUID);

    auditService()
        .createAuditEntry(
            request, Audit.TargetType.Alert, configurationUUID.toString(), Audit.ActionType.Delete);
    return YBPSuccess.empty();
  }

  @ApiOperation(value = "Send test alert for alert configuration", response = YBPSuccess.class)
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result sendTestAlert(UUID customerUUID, UUID configurationUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);

    AlertConfiguration configuration =
        alertConfigurationService.getOrBadRequest(customerUUID, configurationUUID);
    Alert alert = createTestAlert(customer, configuration, true);
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
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.CREATE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result createAlertChannel(UUID customerUUID, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);
    AlertChannelFormData data = parseJsonAndValidate(request, AlertChannelFormData.class);
    AlertChannel channel =
        new AlertChannel().setCustomerUUID(customerUUID).setName(data.name).setParams(data.params);
    alertChannelService.save(channel);
    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.AlertChannel,
            Objects.toString(channel.getUuid(), null),
            Audit.ActionType.Create);
    return PlatformResults.withData(CommonUtils.maskObject(channel));
  }

  @ApiOperation(value = "Get an alert channel", response = AlertChannel.class)
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
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
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result updateAlertChannel(UUID customerUUID, UUID alertChannelUUID, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);
    AlertChannel channel = alertChannelService.getOrBadRequest(customerUUID, alertChannelUUID);
    AlertChannelFormData data = parseJsonAndValidate(request, AlertChannelFormData.class);
    channel
        .setName(data.name)
        .setParams(CommonUtils.unmaskObject(channel.getParams(), data.params));
    alertChannelService.save(channel);
    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.AlertChannel,
            alertChannelUUID.toString(),
            Audit.ActionType.Update);
    return PlatformResults.withData(CommonUtils.maskObject(channel));
  }

  @ApiOperation(value = "Delete an alert channel", response = YBPSuccess.class)
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.DELETE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result deleteAlertChannel(UUID customerUUID, UUID alertChannelUUID, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);
    AlertChannel channel = alertChannelService.getOrBadRequest(customerUUID, alertChannelUUID);
    alertChannelService.delete(customerUUID, alertChannelUUID);
    metricService.markSourceRemoved(channel.getCustomerUUID(), alertChannelUUID);
    auditService()
        .createAuditEntry(
            request,
            Audit.TargetType.AlertChannel,
            alertChannelUUID.toString(),
            Audit.ActionType.Delete);
    return YBPSuccess.empty();
  }

  @ApiOperation(
      value = "List all alert channels",
      response = AlertChannel.class,
      responseContainer = "List")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result listAlertChannels(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);
    return PlatformResults.withData(
        alertChannelService.list(customerUUID).stream()
            .map(channel -> CommonUtils.maskObject(channel))
            .collect(Collectors.toList()));
  }

  @ApiOperation(value = "Get alert channel templates", response = AlertChannelTemplatesExt.class)
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result getAlertChannelTemplates(UUID customerUUID, String channelTypeStr) {
    Customer.getOrBadRequest(customerUUID);
    ChannelType channelType = parseChannelType(channelTypeStr);
    return PlatformResults.withData(
        alertChannelTemplateService.getWithDefaults(customerUUID, channelType));
  }

  @ApiOperation(value = "Set alert channel templates", response = AlertChannelTemplates.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "SetAlertChannelTemplatesRequest",
          paramType = "body",
          dataType = "com.yugabyte.yw.models.AlertChannelTemplates",
          required = true))
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result setAlertChannelTemplates(
      UUID customerUUID, String channelTypeStr, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);
    ChannelType channelType = parseChannelType(channelTypeStr);
    AlertChannelTemplates data = parseJson(request, AlertChannelTemplates.class);
    data.setType(channelType);
    data.setCustomerUUID(customerUUID);
    AlertChannelTemplates result = alertChannelTemplateService.save(data);
    auditService()
        .createAuditEntryWithReqBody(
            request, Audit.TargetType.AlertChannelTemplates, data.getType().name(), ActionType.Set);
    return PlatformResults.withData(CommonUtils.maskObject(result));
  }

  @ApiOperation(value = "Delete alert channel templates", response = YBPSuccess.class)
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.DELETE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result deleteAlertChannelTemplates(
      UUID customerUUID, String channelTypeStr, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);
    ChannelType channelType = parseChannelType(channelTypeStr);
    alertChannelTemplateService.delete(customerUUID, channelType);
    auditService()
        .createAuditEntry(
            request, TargetType.AlertChannelTemplates, channelTypeStr, Audit.ActionType.Delete);
    return YBPSuccess.empty();
  }

  @ApiOperation(
      value = "List all alert channel templates",
      response = AlertChannelTemplatesExt.class,
      responseContainer = "List")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result listAlertChannelTemplates(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);
    return PlatformResults.withData(alertChannelTemplateService.listWithDefaults(customerUUID));
  }

  @ApiOperation(value = "Create an alert destination", response = AlertDestination.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "CreateAlertDestinationRequest",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.AlertDestinationFormData",
          required = true))
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.CREATE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result createAlertDestination(UUID customerUUID, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);
    AlertDestinationFormData data =
        formFactory.getFormDataOrBadRequest(request, AlertDestinationFormData.class).get();
    AlertDestination destination =
        new AlertDestination()
            .setCustomerUUID(customerUUID)
            .setName(data.name)
            .setChannelsList(alertChannelService.getOrBadRequest(customerUUID, data.channels))
            .setDefaultDestination(data.defaultDestination);
    alertDestinationService.save(destination);
    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.AlertDestination,
            Objects.toString(destination.getUuid(), null),
            Audit.ActionType.Create);
    return PlatformResults.withData(destination);
  }

  @ApiOperation(value = "Get an alert destination", response = AlertDestination.class)
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
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
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result updateAlertDestination(
      UUID customerUUID, UUID alertDestinationUUID, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);
    AlertDestinationFormData data =
        formFactory.getFormDataOrBadRequest(request, AlertDestinationFormData.class).get();
    AlertDestination destination =
        alertDestinationService.getOrBadRequest(customerUUID, alertDestinationUUID);
    destination
        .setName(data.name)
        .setDefaultDestination(data.defaultDestination)
        .setChannelsList(alertChannelService.getOrBadRequest(customerUUID, data.channels));
    alertDestinationService.save(destination);
    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.AlertDestination,
            alertDestinationUUID.toString(),
            Audit.ActionType.Update);
    return PlatformResults.withData(destination);
  }

  @ApiOperation(value = "Delete an alert destination", response = YBPSuccess.class)
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.DELETE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result deleteAlertDestination(
      UUID customerUUID, UUID alertDestinationUUID, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);
    alertDestinationService.delete(customerUUID, alertDestinationUUID);
    auditService()
        .createAuditEntry(
            request,
            Audit.TargetType.AlertDestination,
            alertDestinationUUID.toString(),
            Audit.ActionType.Delete);
    return YBPSuccess.empty();
  }

  @ApiOperation(
      value = "List alert destinations",
      response = AlertDefinition.class,
      responseContainer = "List")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result listAlertDestinations(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);
    return PlatformResults.withData(alertDestinationService.listByCustomer(customerUUID));
  }

  @ApiOperation(
      value = "Get alert template settings",
      response = AlertTemplateSettings.class,
      responseContainer = "List")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result listAlertTemplateSettings(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);

    List<AlertTemplateSettings> settings =
        alertTemplateSettingsService.list(
            AlertTemplateSettingsFilter.builder().customerUuid(customerUUID).build());

    return PlatformResults.withData(settings);
  }

  @ApiOperation(
      value = "Crete or update alert template settings list",
      response = AlertTemplateSettings.class,
      responseContainer = "List")
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "EditAlertTemplateSettingsRequest",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.AlertTemplateSettingsFormData",
          required = true))
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result editAlertTemplateSettings(UUID customerUUID, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);

    AlertTemplateSettingsFormData data = parseJson(request, AlertTemplateSettingsFormData.class);

    List<AlertTemplateSettings> settings =
        alertTemplateSettingsService.save(customerUUID, data.settings);

    auditService()
        .createAuditEntryWithReqBody(
            request,
            TargetType.AlertTemplateSettings,
            Objects.toString(customerUUID, null),
            Audit.ActionType.Edit);
    return PlatformResults.withData(settings);
  }

  @ApiOperation(value = "Delete an alert template settings", response = YBPSuccess.class)
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.DELETE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result deleteAlertTemplateSettings(
      UUID customerUUID, UUID settingsUuid, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);
    alertTemplateSettingsService.getOrBadRequest(customerUUID, settingsUuid);
    alertTemplateSettingsService.delete(settingsUuid);
    auditService()
        .createAuditEntry(
            request,
            TargetType.AlertTemplateSettings,
            settingsUuid.toString(),
            Audit.ActionType.Delete);
    return YBPSuccess.empty();
  }

  @ApiOperation(
      value = "List alert template variables",
      response = AlertTemplateVariablesList.class)
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result listAlertTemplateVariables(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);

    List<AlertTemplateVariable> customVariables = alertTemplateVariableService.list(customerUUID);

    AlertTemplateVariablesList alertTemplateVariablesList =
        new AlertTemplateVariablesList()
            .setCustomVariables(customVariables)
            .setSystemVariables(Arrays.asList(AlertTemplateSystemVariable.values()));
    return PlatformResults.withData(alertTemplateVariablesList);
  }

  @ApiOperation(
      value = "Create or update alert template variables",
      response = AlertTemplateVariable.class,
      responseContainer = "List")
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "EditAlertTemplateVariablesRequest",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.AlertTemplateVariablesFormData",
          required = true))
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result editAlertTemplateVariables(UUID customerUUID, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);

    AlertTemplateVariablesFormData data = parseJson(request, AlertTemplateVariablesFormData.class);

    List<AlertTemplateVariable> variables =
        alertTemplateVariableService.save(customerUUID, data.variables);

    auditService()
        .createAuditEntryWithReqBody(
            request,
            TargetType.AlertTemplateVariables,
            Objects.toString(customerUUID, null),
            Audit.ActionType.Edit);
    return PlatformResults.withData(variables);
  }

  @ApiOperation(value = "Delete an alert template variables", response = YBPSuccess.class)
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.DELETE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result deleteAlertTemplateVariables(
      UUID customerUUID, UUID variablesUuid, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);
    alertTemplateVariableService.getOrBadRequest(customerUUID, variablesUuid);
    alertTemplateVariableService.delete(variablesUuid);
    auditService()
        .createAuditEntry(
            request,
            TargetType.AlertTemplateVariables,
            variablesUuid.toString(),
            Audit.ActionType.Delete);
    return YBPSuccess.empty();
  }

  @ApiOperation(
      value = "Prepare alert notification preview",
      response = AlertTemplateVariablesList.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "NotificationPreviewRequest",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.AlertTemplateVariablesFormData",
          required = true))
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result alertNotificationPreview(UUID customerUUID, Request request) {
    Customer customer = Customer.getOrBadRequest(customerUUID);

    NotificationPreviewFormData previewFormData =
        parseJsonAndValidate(request, NotificationPreviewFormData.class);

    AlertConfiguration alertConfiguration =
        alertConfigurationService.getOrBadRequest(
            customerUUID, previewFormData.getAlertConfigUuid());

    AlertChannelTemplatesPreview channelTemplates = previewFormData.getAlertChannelTemplates();
    channelTemplates.getChannelTemplates().setCustomerUUID(customerUUID);
    alertChannelTemplateService.validate(channelTemplates.getChannelTemplates());

    ChannelType channelType = channelTemplates.getChannelTemplates().getType();
    Alert testAlert = createTestAlert(customer, alertConfiguration, false);
    AlertChannel channel = new AlertChannel().setName("Channel name");
    List<AlertTemplateVariable> variables = alertTemplateVariableService.list(customerUUID);
    AlertChannelTemplatesExt templatesExt =
        alertChannelTemplateService.appendDefaults(
            channelTemplates.getChannelTemplates(), customerUUID, channelType);
    Context context = new Context(channel, templatesExt, variables);

    NotificationPreview preview = new NotificationPreview();
    if (channelType.isHasTitle()) {
      preview.setTitle(AlertChannelBase.getNotificationTitle(testAlert, context, false));
      if (StringUtils.isNotEmpty(channelTemplates.getHighlightedTextTemplate())) {
        templatesExt
            .getChannelTemplates()
            .setTitleTemplate(channelTemplates.getHighlightedTitleTemplate());
        preview.setHighlightedTitle(
            AlertChannelBase.getNotificationTitle(testAlert, context, true));
      }
    }
    boolean escapeHtml = channelType == ChannelType.Email;
    preview.setText(AlertChannelBase.getNotificationText(testAlert, context, escapeHtml));
    if (StringUtils.isNotEmpty(channelTemplates.getHighlightedTextTemplate())) {
      templatesExt
          .getChannelTemplates()
          .setTextTemplate(channelTemplates.getHighlightedTextTemplate());
      preview.setHighlightedText(AlertChannelBase.getNotificationText(testAlert, context, true));
    }
    return PlatformResults.withData(preview);
  }

  private AlertData convert(Alert alert) {
    return convert(Collections.singletonList(alert)).get(0);
  }

  private List<AlertData> convert(List<Alert> alerts) {
    List<Alert> alertsWithoutExpressionLabel =
        alerts.stream()
            .filter(alert -> alert.getLabelValue(KnownAlertLabels.ALERT_EXPRESSION) == null)
            .collect(Collectors.toList());
    List<UUID> alertsConfigurationUuids =
        alertsWithoutExpressionLabel.stream()
            .map(Alert::getConfigurationUuid)
            .collect(Collectors.toList());
    List<UUID> alertDefinitionUuids =
        alertsWithoutExpressionLabel.stream()
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
                .list(AlertDefinitionFilter.builder().uuids(alertDefinitionUuids).build()).stream()
                .collect(Collectors.toMap(AlertDefinition::getUuid, Function.identity()))
            : Collections.emptyMap();
    return alerts.stream()
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
        .setAlertExpressionUrl(getAlertExpressionUrl(alert, alertConfiguration, alertDefinition))
        .setMetricsLinkUseBrowserFqdn(metricUrlProvider.getMetricsLinkUseBrowserFqdn());
  }

  private String getAlertExpressionUrl(
      Alert alert, AlertConfiguration alertConfiguration, AlertDefinition alertDefinition) {
    String expression = alert.getLabelValue(KnownAlertLabels.ALERT_EXPRESSION);
    if (expression == null) {
      // Old resolved alerts may not have alert expression.
      // Will try to get from configuration and definition.
      // Thresholds and even expression itself may already be changed - but that's best we can do.
      if (alertConfiguration != null && alertDefinition != null) {
        AlertTemplateDescription description =
            alertTemplateService.getTemplateDescription(alertConfiguration.getTemplate());
        expression =
            description.getQueryWithThreshold(
                alertDefinition, alertConfiguration.getThresholds().get(alert.getSeverity()));
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

  @VisibleForTesting
  Alert createTestAlert(
      Customer customer, AlertConfiguration configuration, boolean testAlertPrefix) {
    AlertTemplateDescription alertTemplateDescription =
        alertTemplateService.getTemplateDescription(configuration.getTemplate());
    AlertDefinition definition =
        alertDefinitionService
            .list(
                AlertDefinitionFilter.builder().configurationUuid(configuration.getUuid()).build())
            .stream()
            .findFirst()
            .orElse(null);
    if (definition == null) {
      if (configuration.getTargetType() == AlertConfiguration.TargetType.UNIVERSE) {
        Universe universe = new Universe();
        universe.setUniverseUUID(UUID.randomUUID());
        UniverseDefinitionTaskParams details = new UniverseDefinitionTaskParams();
        details.nodePrefix = "node_prefix";
        universe.setUniverseDetails(details);
        definition = new AlertDefinition();
        definition.setCustomerUUID(customer.getUuid());
        definition.setConfigurationUUID(configuration.getUuid());
        definition.generateUUID();
        definition.setLabels(
            MetricLabelsBuilder.create()
                .appendCustomer(customer)
                .appendSource(getOrCreateUniverseForTestAlert(customer))
                .getDefinitionLabels());
      } else {
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR, "Missing definition for Platform alert configuration");
      }
    }

    Severity severity =
        configuration.getThresholds().containsKey(Severity.SEVERE)
            ? Severity.SEVERE
            : Severity.WARNING;
    AlertTemplateSettings alertTemplateSettings =
        alertTemplateSettingsService.get(
            configuration.getCustomerUUID(), configuration.getTemplate().name());
    List<AlertLabel> labels =
        definition
            .getEffectiveLabels(
                alertTemplateDescription, configuration, alertTemplateSettings, severity)
            .stream()
            .map(label -> new AlertLabel(label.getName(), label.getValue()))
            .collect(Collectors.toList());
    labels.add(new AlertLabel(KnownAlertLabels.ALERTNAME.labelName(), configuration.getName()));
    labels.addAll(
        alertTemplateDescription.getTestAlertSettings().getAdditionalLabels().stream()
            .map(label -> new AlertLabel(label.getName(), label.getValue()))
            .toList());
    Map<String, String> alertLabels =
        labels.stream().collect(Collectors.toMap(AlertLabel::getName, AlertLabel::getValue));
    Alert alert =
        new Alert()
            .generateUUID()
            .setCreateTime(new Date())
            .setCustomerUUID(configuration.getCustomerUUID())
            .setDefinitionUuid(definition.getUuid())
            .setConfigurationUuid(configuration.getUuid())
            .setName(configuration.getName())
            .setSourceName(alertLabels.get(KnownAlertLabels.SOURCE_NAME.labelName()))
            .setSeverity(severity)
            .setConfigurationType(configuration.getTargetType())
            .setLabels(labels);
    String sourceUuid = alertLabels.get(KnownAlertLabels.SOURCE_UUID.labelName());
    if (StringUtils.isNotEmpty(sourceUuid)) {
      alert.setSourceUUID(UUID.fromString(sourceUuid));
    }
    if (alertTemplateDescription.getLabels().containsKey(AFFECTED_NODE_NAMES)) {
      alert.setLabel(AFFECTED_NODE_NAMES, "node1 node2 node3");
      alert.setLabel(AFFECTED_NODE_ADDRESSES, "1.2.3.1 1.2.3.2 1.2.3.3");
      alert.setLabel(AFFECTED_NODE_IDENTIFIERS, "node1 node2 node3");
    }
    if (alertTemplateDescription.getLabels().containsKey(AFFECTED_VOLUMES)) {
      alert.setLabel(AFFECTED_VOLUMES, "node1:/\nnode2:/\n");
    }
    alert.setMessage(
        buildTestAlertMessage(alertTemplateDescription, configuration, alert, testAlertPrefix));
    return alert;
  }

  private String buildTestAlertMessage(
      AlertTemplateDescription alertTemplateDescription,
      AlertConfiguration configuration,
      Alert alert,
      boolean testAlertPrefix) {

    TestAlertSettings settings = alertTemplateDescription.getTestAlertSettings();
    if (settings.getCustomMessage() != null) {
      return settings.getCustomMessage();
    }
    String messageTemplate = alertTemplateDescription.getAnnotations().get("summary");
    AlertTemplateSubstitutor<Alert> alertTemplateSubstitutor =
        new AlertTemplateSubstitutor<>(alert);
    String message = alertTemplateSubstitutor.replace(messageTemplate);
    TestAlertTemplateSubstitutor testAlertTemplateSubstitutor =
        new TestAlertTemplateSubstitutor(alert, alertTemplateDescription, configuration);
    message = testAlertTemplateSubstitutor.replace(message);
    return testAlertPrefix ? "[TEST ALERT!!!] " + message : message;
  }

  private Universe getOrCreateUniverseForTestAlert(Customer customer) {
    Set<Universe> allUniverses = Universe.getAllWithoutResources(customer);
    Universe firstUniverse =
        allUniverses.stream()
            .filter(universe -> universe.getUniverseDetails().nodePrefix != null)
            .min(Comparator.comparing(universe -> universe.getCreationDate()))
            .orElse(null);
    if (firstUniverse != null) {
      return firstUniverse;
    }
    Universe universe = new Universe();
    universe.setName("some-universe");
    universe.setUniverseUUID(UUID.randomUUID());
    UniverseDefinitionTaskParams universeDefinitionTaskParams = new UniverseDefinitionTaskParams();
    universeDefinitionTaskParams.nodePrefix = "some-universe-node";
    universe.setUniverseDetails(universeDefinitionTaskParams);
    return universe;
  }

  private ChannelType parseChannelType(String channelTypeStr) {
    try {
      return ChannelType.valueOf(channelTypeStr);
    } catch (Exception e) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Channel type " + channelTypeStr + " does not exist");
    }
  }
}
