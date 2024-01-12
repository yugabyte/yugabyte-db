/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.controllers;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.api.client.util.Throwables;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.CloudBootstrap;
import com.yugabyte.yw.common.CloudProviderHelper;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.controllers.handlers.CloudProviderHandler;
import com.yugabyte.yw.forms.EditAccessKeyRotationScheduleParams;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.forms.PlatformResults.YBPTask;
import com.yugabyte.yw.forms.RotateAccessKeyFormData;
import com.yugabyte.yw.forms.ScheduledAccessKeyRotateFormData;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.common.YbaApi.YbaApiVisibility;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.models.helpers.provider.KubernetesInfo;
import com.yugabyte.yw.models.helpers.provider.region.RegionMetadata;
import com.yugabyte.yw.rbac.annotations.AuthzPath;
import com.yugabyte.yw.rbac.annotations.PermissionAttribute;
import com.yugabyte.yw.rbac.annotations.RequiredPermissionOnResource;
import com.yugabyte.yw.rbac.annotations.Resource;
import com.yugabyte.yw.rbac.enums.SourceType;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiImplicitParam;
import io.swagger.annotations.ApiImplicitParams;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Result;

@Api(
    value = "Cloud providers",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
@Slf4j
public class CloudProviderApiController extends AuthenticatedController {

  @Inject private CloudProviderHandler cloudProviderHandler;
  @Inject private CloudProviderHelper cloudProviderHelper;
  @Inject private RuntimeConfigFactory runtimeConfigFactory;
  @Inject private ConfigHelper configHelper;

  @ApiOperation(
      value = "List cloud providers",
      response = Provider.class,
      responseContainer = "List",
      nickname = "getListOfProviders")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result list(UUID customerUUID, String name, String code) {
    CloudType providerCode = code == null ? null : CloudType.valueOf(code);
    List<Provider> providers = Provider.getAll(customerUUID, name, providerCode);
    providers.forEach(CloudInfoInterface::mayBeMassageResponse);
    return PlatformResults.withData(providers);
  }

  @ApiOperation(value = "Get a cloud provider", response = Provider.class, nickname = "getProvider")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result index(UUID customerUUID, UUID providerUUID) {
    Customer.getOrBadRequest(customerUUID);
    Provider provider = Provider.getOrBadRequest(customerUUID, providerUUID);
    CloudInfoInterface.mayBeMassageResponse(provider);
    return PlatformResults.withData(provider);
  }

  @ApiOperation(value = "Delete a cloud provider", response = YBPTask.class)
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.DELETE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result delete(UUID customerUUID, UUID providerUUID, Http.Request request) {
    Customer customer = Customer.getOrBadRequest(customerUUID);

    UUID taskUUID = cloudProviderHandler.delete(customer, providerUUID);
    auditService()
        .createAuditEntry(
            request,
            Audit.TargetType.CloudProvider,
            providerUUID.toString(),
            Audit.ActionType.Delete,
            taskUUID);
    return new YBPTask(taskUUID, providerUUID).asResult();
  }

  @ApiOperation(
      notes = "WARNING: This is a preview API that could change.",
      value = "Refresh provider pricing info",
      response = YBPSuccess.class)
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.20.0.0")
  public Result refreshPricing(UUID customerUUID, UUID providerUUID, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);
    Provider provider = Provider.getOrBadRequest(customerUUID, providerUUID);
    cloudProviderHandler.refreshPricing(customerUUID, provider);
    auditService()
        .createAuditEntry(
            request,
            Audit.TargetType.CloudProvider,
            providerUUID.toString(),
            Audit.ActionType.RefreshPricing);
    return YBPSuccess.withMessage(provider.getCode().toUpperCase() + " Initialized");
  }

  @ApiOperation(value = "Update a provider", response = YBPTask.class, nickname = "editProvider")
  @ApiImplicitParams(
      @ApiImplicitParam(
          value = "edit provider form data",
          name = "EditProviderRequest",
          dataType = "com.yugabyte.yw.models.Provider",
          required = true,
          paramType = "body"))
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result edit(
      UUID customerUUID,
      UUID providerUUID,
      boolean validate,
      boolean ignoreValidationErrors,
      Http.Request request) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Provider provider = Provider.getOrBadRequest(customerUUID, providerUUID);
    JsonNode requestBody = mayBeMassageRequest(request.body().asJson(), true);

    Provider editProviderReq = formFactory.getFormDataOrBadRequest(requestBody, Provider.class);
    UUID taskUUID =
        cloudProviderHandler.editProvider(
            customer, provider, editProviderReq, validate, ignoreValidationErrors);
    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.CloudProvider,
            providerUUID.toString(),
            Audit.ActionType.Update);
    return new YBPTask(taskUUID, providerUUID).asResult();
  }

  @ApiOperation(value = "Create a provider", response = YBPTask.class, nickname = "createProviders")
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "CreateProviderRequest",
          paramType = "body",
          dataType = "com.yugabyte.yw.models.Provider",
          required = true))
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.CREATE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result create(
      UUID customerUUID, boolean validate, boolean ignoreValidationErrors, Http.Request request) {
    JsonNode requestBody = mayBeMassageRequest(request.body().asJson(), false);
    Provider reqProvider =
        formFactory.getFormDataOrBadRequest(request.body().asJson(), Provider.class);
    Customer customer = Customer.getOrBadRequest(customerUUID);
    reqProvider.setCustomerUUID(customerUUID);
    CloudType providerCode = CloudType.valueOf(reqProvider.getCode());
    Provider existingProvider =
        Provider.get(customer.getUuid(), reqProvider.getName(), providerCode);
    if (existingProvider != null) {
      throw new PlatformServiceException(
          CONFLICT,
          String.format("Provider with the name %s already exists", reqProvider.getName()));
    }

    Provider providerEbean;
    if (providerCode.equals(CloudType.kubernetes)) {
      /*
       * Marking the k8s providers created starting from YBA version 2.18
       * as non legacy. The default behaviour of this flag is `true` indicating
       * the legacy provider.
       *
       * This will be removed once we fix the old providers & can safely start returning
       * the merged configs.
       */
      KubernetesInfo k8sInfo = CloudInfoInterface.get(reqProvider);
      k8sInfo.setLegacyK8sProvider(false);
      providerEbean =
          Provider.create(
              customer.getUuid(), providerCode, reqProvider.getName(), reqProvider.getDetails());
    } else {
      providerEbean =
          cloudProviderHandler.createProvider(
              customer,
              providerCode,
              reqProvider.getName(),
              reqProvider,
              validate,
              ignoreValidationErrors);
    }

    if (providerCode.isRequiresBootstrap()) {
      UUID taskUUID = null;
      try {
        CloudBootstrap.Params taskParams =
            CloudBootstrap.Params.fromProvider(providerEbean, reqProvider);
        if (providerEbean.getCloudCode() == CloudType.kubernetes) {
          taskParams.reqProviderEbean = reqProvider;
        }

        taskUUID = cloudProviderHandler.bootstrap(customer, providerEbean, taskParams);
        auditService()
            .createAuditEntryWithReqBody(
                request,
                Audit.TargetType.CloudProvider,
                Objects.toString(providerEbean.getUuid(), null),
                Audit.ActionType.Create,
                requestBody,
                taskUUID);
      } catch (Throwable e) {
        log.warn("Bootstrap failed. Deleting provider");
        providerEbean.delete();
        Throwables.propagate(e);
      }
      return new YBPTask(taskUUID, providerEbean.getUuid()).asResult();
    } else {
      auditService()
          .createAuditEntryWithReqBody(
              request,
              Audit.TargetType.CloudProvider,
              Objects.toString(providerEbean.getUuid(), null),
              Audit.ActionType.Create,
              requestBody,
              null);
      return new YBPTask(null, providerEbean.getUuid()).asResult();
    }
  }

  @ApiOperation(
      nickname = "accessKeyRotation",
      notes = "WARNING: This is a preview API that could change.",
      value = "Rotate access key for a provider",
      response = YBPTask.class)
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.20.0.0")
  public Result accessKeysRotation(UUID customerUUID, UUID providerUUID, Http.Request request) {
    RotateAccessKeyFormData params = parseJsonAndValidate(request, RotateAccessKeyFormData.class);
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Provider.getOrBadRequest(customerUUID, providerUUID);
    String newKeyCode = params.newKeyCode;
    boolean rotateAllUniverses = params.rotateAllUniverses;
    if (!rotateAllUniverses && params.universeUUIDs.size() == 0) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Need to specify universeUUIDs"
              + " for access key rotation or set rotateAllUniverses to true!");
    }
    List<UUID> universeUUIDs =
        rotateAllUniverses
            ? customer.getUniversesForProvider(providerUUID).stream()
                .map(universe -> universe.getUniverseUUID())
                .collect(Collectors.toList())
            : params.universeUUIDs;

    Map<UUID, UUID> tasks =
        cloudProviderHandler.rotateAccessKeys(
            customerUUID, providerUUID, universeUUIDs, newKeyCode);

    // contains taskUUID and resourceUUID (universeUUID) for each universe
    List<YBPTask> tasksResponseList = new ArrayList<>();
    tasks.forEach(
        (universeUUID, taskUUID) -> tasksResponseList.add(new YBPTask(taskUUID, universeUUID)));
    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.CloudProvider,
            Objects.toString(providerUUID, null),
            Audit.ActionType.RotateAccessKey);
    return PlatformResults.withData(tasksResponseList);
  }

  @ApiOperation(
      nickname = "scheduledAccessKeyRotation",
      notes = "WARNING: This is a preview API that could change.",
      value = "Rotate access key for a provider - Scheduled",
      response = Schedule.class)
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.20.0.0")
  public Result scheduledAccessKeysRotation(
      UUID customerUUID, UUID providerUUID, Http.Request request) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Provider.getOrBadRequest(customerUUID, providerUUID);
    ScheduledAccessKeyRotateFormData params =
        parseJsonAndValidate(request, ScheduledAccessKeyRotateFormData.class);
    int schedulingFrequencyDays = params.schedulingFrequencyDays;
    boolean rotateAllUniverses = params.rotateAllUniverses;
    if (!rotateAllUniverses && params.universeUUIDs.size() == 0) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Need to specify universeUUIDs"
              + " to schedule access key rotation or set rotateAllUniverses to true!");
    }
    List<UUID> universeUUIDs =
        rotateAllUniverses
            ? customer.getUniversesForProvider(providerUUID).stream()
                .map(universe -> universe.getUniverseUUID())
                .collect(Collectors.toList())
            : params.universeUUIDs;
    Schedule schedule =
        cloudProviderHandler.scheduleAccessKeysRotation(
            customerUUID, providerUUID, universeUUIDs, schedulingFrequencyDays, rotateAllUniverses);
    UUID scheduleUUID = schedule.getScheduleUUID();
    log.info(
        "Created access key rotation schedule for customer {}, schedule uuid = {}.",
        customerUUID,
        scheduleUUID);
    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.CloudProvider,
            Objects.toString(providerUUID, null),
            Audit.ActionType.CreateAndRotateAccessKey);
    return PlatformResults.withData(schedule);
  }

  @ApiOperation(
      notes = "WARNING: This is a preview API that could change.",
      value = "List all schedules for a provider's" + " access key rotation",
      response = Schedule.class,
      responseContainer = "List",
      nickname = "listSchedules")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.20.0.0")
  public Result listAccessKeyRotationSchedules(UUID customerUUID, UUID providerUUID) {
    Customer.getOrBadRequest(customerUUID);
    Provider.getOrBadRequest(customerUUID, providerUUID);
    List<Schedule> accessKeyRotationSchedules =
        Schedule.getAllByCustomerUUIDAndType(customerUUID, TaskType.CreateAndRotateAccessKey)
            .stream()
            .filter(schedule -> (schedule.getOwnerUUID().equals(providerUUID)))
            .collect(Collectors.toList());
    return PlatformResults.withData(accessKeyRotationSchedules);
  }

  @ApiOperation(
      notes = "WARNING: This is a preview API that could change.",
      value = "Edit a access key rotation schedule",
      response = Schedule.class,
      nickname = "editAccessKeyRotationSchedule")
  @ApiImplicitParams({
    @ApiImplicitParam(
        required = true,
        dataType = "com.yugabyte.yw.forms.EditAccessKeyRotationScheduleParams",
        paramType = "body")
  })
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.20.0.0")
  public Result editAccessKeyRotationSchedule(
      UUID customerUUID, UUID providerUUID, UUID scheduleUUID, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);
    Provider.getOrBadRequest(customerUUID, providerUUID);
    EditAccessKeyRotationScheduleParams params =
        parseJsonAndValidate(request, EditAccessKeyRotationScheduleParams.class);

    Schedule schedule =
        cloudProviderHandler.editAccessKeyRotationSchedule(
            customerUUID, providerUUID, scheduleUUID, params);

    auditService()
        .createAuditEntryWithReqBody(
            request, Audit.TargetType.Schedule, scheduleUUID.toString(), Audit.ActionType.Edit);
    return PlatformResults.withData(schedule);
  }

  @ApiOperation(
      value = "Retrieves the region metadata for the cloud providers",
      response = RegionMetadata.class,
      nickname = "getRegionMetadata")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result fetchRegionMetadata(UUID customerUUID, String code, String subType) {
    CloudType cloudType = null;
    try {
      cloudType = CloudType.valueOf(code);
      if (cloudType == CloudType.kubernetes && StringUtils.isEmpty(subType)) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            String.format("Specify a subType (eks, gke, aks, etc) for %s provider.", code));
      }
    } catch (IllegalArgumentException e) {
      throw new PlatformServiceException(BAD_REQUEST, "Specify a valid cloud provider code.");
    }
    try {
      Map<String, Object> regionMetadataMap;
      if (cloudType != CloudType.kubernetes) {
        regionMetadataMap = configHelper.getRegionMetadata(cloudType);
      } else {
        ConfigHelper.ConfigType k8sConfigType =
            cloudProviderHelper.getKubernetesConfigType(subType);
        regionMetadataMap = configHelper.getConfig(k8sConfigType);
      }
      ObjectMapper mapper = Json.mapper();
      ObjectNode regionMetadataObj = mapper.createObjectNode();
      regionMetadataObj.set("regionMetadata", Json.toJson(regionMetadataMap));
      RegionMetadata regionMetadata =
          mapper.readValue(Json.toJson(regionMetadataObj).toString(), RegionMetadata.class);
      return PlatformResults.withData(regionMetadata);
    } catch (Exception e) {
      log.debug(
          String.format("Transaltion to regionMetadata object failed with %s", e.getMessage()));
    }
    return PlatformResults.withData(null);
  }

  // v2 API version 1 backward compatibility support.
  private JsonNode mayBeMassageRequest(JsonNode requestBody, Boolean forEdit) {
    JsonNode config = requestBody.get("config");
    if (forEdit && config != null) {
      ((ObjectNode) requestBody).remove("config");
      // Clear the deprecated top level fields that are supported only on create.
      // Edit is a new API and we wont allow changing these fields at top-level during
      // the edit operation.
      ((ObjectNode) requestBody).remove("sshUser");
      ((ObjectNode) requestBody).remove("sshPort");
      ((ObjectNode) requestBody).remove("airGapInstall");
      ((ObjectNode) requestBody).remove("ntpServers");
      ((ObjectNode) requestBody).remove("setUpChrony");
      ((ObjectNode) requestBody).remove("showSetUpChrony");
      ((ObjectNode) requestBody).remove("keyPairName");
      ((ObjectNode) requestBody).remove("sshPrivateKeyContent");
    }
    String providerCode = requestBody.get("code").asText();
    if (providerCode.equals(CloudType.gcp.name())) {
      // This is to keep the older API clients happy in case they still continue to use
      // JSON object.
      ObjectMapper mapper = Json.mapper();
      ObjectNode details = (ObjectNode) requestBody.get("details");
      if (details.has("cloudInfo")) {
        ObjectNode cloudInfo = (ObjectNode) details.get("cloudInfo");
        if (cloudInfo.has("gcp")) {
          ObjectNode gcpCloudInfo = (ObjectNode) cloudInfo.get("gcp");
          try {
            if (gcpCloudInfo.has("gceApplicationCredentials")
                && !(gcpCloudInfo.get("gceApplicationCredentials").isTextual())) {
              gcpCloudInfo.put(
                  "gceApplicationCredentials",
                  mapper.writeValueAsString(gcpCloudInfo.get("gceApplicationCredentials")));
            }
          } catch (Exception e) {
            throw new PlatformServiceException(
                INTERNAL_SERVER_ERROR, "Failed to read GCP Service Account Credentials");
          }
          cloudInfo.set("gcp", gcpCloudInfo);
          details.set("cloudInfo", cloudInfo);
          ((ObjectNode) requestBody).set("details", details);
        }
      }
    }
    ObjectMapper mapper = Json.mapper();
    JsonNode regions = requestBody.get("regions");
    ArrayNode regionsNode = mapper.createArrayNode();
    if (regions != null && regions.isArray()) {
      for (JsonNode region : regions) {
        ObjectNode regionWithProviderCode = mapper.createObjectNode();
        regionWithProviderCode.put("providerCode", providerCode);
        if (region.has("config") && forEdit) {
          ((ObjectNode) region).remove("config");
        }
        regionWithProviderCode.setAll((ObjectNode) region);
        JsonNode zones = region.get("zones");
        ArrayNode zonesNode = mapper.createArrayNode();
        if (zones != null && zones.isArray()) {
          for (JsonNode zone : zones) {
            ObjectNode zoneWithProviderCode = mapper.createObjectNode();
            if (zone.has("config") && forEdit) {
              ((ObjectNode) zone).remove("config");
            }
            zoneWithProviderCode.put("providerCode", providerCode);
            zoneWithProviderCode.setAll((ObjectNode) zone);
            zonesNode.add(zoneWithProviderCode);
          }
        }
        regionWithProviderCode.remove("zones");
        regionWithProviderCode.put("zones", zonesNode);
        regionsNode.add(regionWithProviderCode);
      }
    }
    ((ObjectNode) requestBody).remove("regions");
    ((ObjectNode) requestBody).put("regions", regionsNode);

    return CloudInfoInterface.mayBeMassageRequest(requestBody, true);
  }
}
