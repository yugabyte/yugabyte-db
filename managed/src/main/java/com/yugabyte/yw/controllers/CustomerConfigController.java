// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.tasks.DeleteCustomerConfig;
import com.yugabyte.yw.commissioner.tasks.DeleteCustomerStorageConfig;
import com.yugabyte.yw.common.CloudUtil;
import com.yugabyte.yw.common.CloudUtilFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.customer.config.CustomerConfigService;
import com.yugabyte.yw.common.customer.config.CustomerConfigUI;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.forms.PlatformResults.YBPTask;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.configs.CustomerConfig.ConfigState;
import com.yugabyte.yw.models.configs.data.CustomerConfigData;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.TaskType;
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
import java.util.Objects;
import java.util.UUID;
import javax.inject.Inject;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.mvc.Http;
import play.mvc.Result;

@Api(
    value = "Customer Configuration",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class CustomerConfigController extends AuthenticatedController {
  public static final Logger LOG = LoggerFactory.getLogger(CustomerConfigController.class);

  private final CustomerConfigService customerConfigService;
  private final CloudUtilFactory cloudUtilFactory;

  @Inject
  public CustomerConfigController(
      CustomerConfigService customerConfigService, CloudUtilFactory cloudUtilFactory) {
    this.customerConfigService = customerConfigService;
    this.cloudUtilFactory = cloudUtilFactory;
  }

  @Inject Commissioner commissioner;

  @ApiOperation(
      value = "Create a customer configuration",
      response = CustomerConfig.class,
      nickname = "createCustomerConfig")
  @ApiImplicitParams({
    @ApiImplicitParam(
        name = "Config",
        value = "Configuration data to be created",
        required = true,
        dataType = "com.yugabyte.yw.models.configs.CustomerConfig",
        paramType = "body")
  })
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.CREATE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result create(UUID customerUUID, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);
    CustomerConfig customerConfig = parseJson(request, CustomerConfig.class);
    customerConfig.setCustomerUUID(customerUUID);

    customerConfigService.create(customerConfig);
    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.CustomerConfig,
            Objects.toString(customerConfig.getConfigUUID(), null),
            Audit.ActionType.Create);
    return PlatformResults.withData(this.customerConfigService.getConfigMasked(customerConfig));
  }

  @ApiOperation(
      value = "Delete a customer configuration",
      response = YBPTask.class,
      nickname = "deleteCustomerConfig")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.DELETE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result delete(
      UUID customerUUID, UUID configUUID, boolean isDeleteBackups, Http.Request request) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    CustomerConfig customerConfig = customerConfigService.getOrBadRequest(customerUUID, configUUID);
    if (customerConfig.getType() == CustomerConfig.ConfigType.STORAGE) {
      Boolean backupsInProgress = Backup.findIfBackupsRunningWithCustomerConfig(configUUID);
      if (backupsInProgress) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            "Backup task associated with Configuration "
                + configUUID.toString()
                + " is in progress.");
      }

      // If storageConfig is used by DR, do not allow deletion.
      if (customerConfig.isStorageConfigUsedForDr()) {
        throw new PlatformServiceException(
            BAD_REQUEST, "DR Config is associated with Configuration " + configUUID.toString());
      }
      if (isDeleteBackups) {
        DeleteCustomerConfig.Params taskParams = new DeleteCustomerConfig.Params();
        taskParams.customerUUID = customerUUID;
        taskParams.configUUID = configUUID;
        taskParams.isDeleteBackups = isDeleteBackups;
        UUID taskUUID = commissioner.submit(TaskType.DeleteCustomerConfig, taskParams);
        LOG.info(
            "Saved task uuid {} in customer tasks for Customer Configuration {}.",
            taskUUID,
            configUUID);
        CustomerTask.create(
            customer,
            configUUID,
            taskUUID,
            CustomerTask.TargetType.CustomerConfiguration,
            CustomerTask.TaskType.Delete,
            customerConfig.getConfigName());
        auditService()
            .createAuditEntry(
                request,
                Audit.TargetType.CustomerConfig,
                configUUID.toString(),
                Audit.ActionType.Delete);
        return new YBPTask(taskUUID, configUUID).asResult();
      }
    }
    customerConfigService.delete(customerUUID, configUUID);

    auditService()
        .createAuditEntry(
            request,
            Audit.TargetType.CustomerConfig,
            configUUID.toString(),
            Audit.ActionType.Delete);
    return YBPSuccess.withMessage("Config " + configUUID + " deleted");
  }

  @ApiOperation(
      value = "Delete a customer configuration V2",
      response = YBPTask.class,
      nickname = "deleteCustomerConfigV2")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.DELETE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result deleteYb(
      UUID customerUUID, UUID configUUID, boolean isDeleteBackups, Http.Request request) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    CustomerConfig customerConfig = customerConfigService.getOrBadRequest(customerUUID, configUUID);
    if (customerConfig.getType() == CustomerConfig.ConfigType.STORAGE) {
      Boolean backupsInProgress = Backup.findIfBackupsRunningWithCustomerConfig(configUUID);
      if (backupsInProgress) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            "Backup task associated with Configuration "
                + configUUID.toString()
                + " is in progress.");
      }

      // If storageConfig is used by DR, do not allow deletion.
      if (customerConfig.isStorageConfigUsedForDr()) {
        throw new PlatformServiceException(
            BAD_REQUEST, "DR Config is associated with Configuration " + configUUID.toString());
      }

      DeleteCustomerStorageConfig.Params taskParams = new DeleteCustomerStorageConfig.Params();
      taskParams.customerUUID = customerUUID;
      taskParams.configUUID = configUUID;
      taskParams.isDeleteBackups = isDeleteBackups;
      UUID taskUUID = commissioner.submit(TaskType.DeleteCustomerStorageConfig, taskParams);
      LOG.info(
          "Saved task uuid {} in customer tasks for Customer Configuration {}.",
          taskUUID,
          configUUID);
      CustomerTask.create(
          customer,
          configUUID,
          taskUUID,
          CustomerTask.TargetType.CustomerConfiguration,
          CustomerTask.TaskType.Delete,
          customerConfig.getConfigName());
      auditService()
          .createAuditEntry(
              request,
              Audit.TargetType.CustomerConfig,
              configUUID.toString(),
              Audit.ActionType.Delete);
      return new YBPTask(taskUUID, configUUID).asResult();
    }
    customerConfigService.delete(customerUUID, configUUID);
    auditService()
        .createAuditEntry(
            request,
            Audit.TargetType.CustomerConfig,
            configUUID.toString(),
            Audit.ActionType.Delete);
    return YBPSuccess.withMessage("Config " + configUUID + " is queued for deletion");
  }

  @ApiOperation(
      value = "List all customer configurations",
      response = CustomerConfigUI.class,
      responseContainer = "List",
      nickname = "getListOfCustomerConfig")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result list(UUID customerUUID) {
    return PlatformResults.withData(customerConfigService.listForUI(customerUUID));
  }

  @ApiOperation(
      value = "Update a customer configuration",
      response = CustomerConfig.class,
      nickname = "editCustomerConfig")
  @ApiImplicitParams({
    @ApiImplicitParam(
        name = "Config",
        value = "Configuration data to be updated",
        required = true,
        dataType = "com.yugabyte.yw.models.configs.CustomerConfig",
        paramType = "body")
  })
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result edit(UUID customerUUID, UUID configUUID, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);
    CustomerConfig customerConfig = parseJson(request, CustomerConfig.class);
    customerConfig.setConfigUUID(configUUID);
    customerConfig.setCustomerUUID(customerUUID);

    CustomerConfig existingConfig = customerConfigService.getOrBadRequest(customerUUID, configUUID);
    CustomerConfig unmaskedConfig = CommonUtils.unmaskObject(existingConfig, customerConfig);

    customerConfigService.edit(unmaskedConfig);

    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.CustomerConfig,
            Objects.toString(customerConfig.getConfigUUID(), null),
            Audit.ActionType.Update);
    return PlatformResults.withData(this.customerConfigService.getConfigMasked(unmaskedConfig));
  }

  @ApiOperation(
      value = "Update a customer configuration V2",
      response = CustomerConfig.class,
      nickname = "editCustomerConfig")
  @ApiImplicitParams({
    @ApiImplicitParam(
        name = "Config",
        value = "Configuration data to be updated",
        required = true,
        dataType = "com.yugabyte.yw.models.configs.CustomerConfig",
        paramType = "body")
  })
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result editYb(UUID customerUUID, UUID configUUID, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);
    CustomerConfig existingConfig = customerConfigService.getOrBadRequest(customerUUID, configUUID);
    if (existingConfig.getState().equals(ConfigState.QueuedForDeletion)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot edit config as it is queued for deletion.");
    }

    CustomerConfig customerConfig = parseJson(request, CustomerConfig.class);
    customerConfig.setConfigUUID(configUUID);
    customerConfig.setCustomerUUID(customerUUID);
    CustomerConfig unmaskedConfig = CommonUtils.unmaskObject(existingConfig, customerConfig);
    customerConfigService.edit(unmaskedConfig);

    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.CustomerConfig,
            Objects.toString(customerConfig.getConfigUUID(), null),
            Audit.ActionType.Update);
    return PlatformResults.withData(this.customerConfigService.getConfigMasked(unmaskedConfig));
  }

  @ApiOperation(
      value = "List buckets with provided credentials",
      response = Object.class,
      nickname = "listBuckets")
  @ApiImplicitParams({
    @ApiImplicitParam(
        name = "Credentials",
        value = "Credentials to list buckets",
        required = true,
        dataType = "com.yugabyte.yw.models.configs.data.CustomerConfigData",
        paramType = "body")
  })
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result listBuckets(UUID customerUUID, String cloud, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);
    CustomerConfigData configData = null;
    try {
      Class<? extends CustomerConfigData> configClass =
          CustomerConfig.getDataClass(CustomerConfig.ConfigType.STORAGE, cloud);
      configData = parseJson(request, configClass);
    } catch (NullPointerException e) {
      throw new PlatformServiceException(
          BAD_REQUEST, String.format("Unsupported cloud type %s", cloud));
    }
    CloudUtil cloudUtil = cloudUtilFactory.getCloudUtil(cloud);
    return PlatformResults.withData(cloudUtil.listBuckets(configData));
  }
}
