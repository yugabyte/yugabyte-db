// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.customer.config.CustomerConfigService;
import com.yugabyte.yw.common.customer.config.CustomerConfigUI;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPTask;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.models.Backup;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerConfig;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.CustomerConfigValidator;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.CustomerConfig.ConfigState;
import com.yugabyte.yw.commissioner.tasks.DeleteCustomerConfig;
import com.yugabyte.yw.commissioner.tasks.DeleteCustomerStorageConfig;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiImplicitParam;
import io.swagger.annotations.ApiImplicitParams;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import java.util.UUID;
import javax.inject.Inject;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.mvc.Result;

@Api(
    value = "Customer Configuration",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class CustomerConfigController extends AuthenticatedController {
  public static final Logger LOG = LoggerFactory.getLogger(CustomerConfigController.class);

  private final CustomerConfigService customerConfigService;

  @Inject
  public CustomerConfigController(CustomerConfigService customerConfigService) {
    this.customerConfigService = customerConfigService;
  }

  @Inject Commissioner commissioner;

  @Inject CustomerConfigValidator configValidator;

  @ApiOperation(
      value = "Create a customer configuration",
      response = CustomerConfig.class,
      nickname = "createCustomerConfig")
  @ApiImplicitParams({
    @ApiImplicitParam(
        name = "Config",
        value = "Configuration data to be created",
        required = true,
        dataType = "com.yugabyte.yw.models.CustomerConfig",
        paramType = "body")
  })
  public Result create(UUID customerUUID) {
    CustomerConfig customerConfig = parseJson(CustomerConfig.class);
    customerConfig.setCustomerUUID(customerUUID);

    customerConfigService.create(customerConfig);

    auditService().createAuditEntry(ctx(), request());
    return PlatformResults.withData(customerConfig);
  }

  @ApiOperation(
      value = "Delete a customer configuration",
      response = YBPTask.class,
      nickname = "deleteCustomerConfig")
  public Result delete(UUID customerUUID, UUID configUUID, boolean isDeleteBackups) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    CustomerConfig customerConfig = customerConfigService.getOrBadRequest(customerUUID, configUUID);
    if (customerConfig.type == CustomerConfig.ConfigType.STORAGE) {
      Boolean backupsInProgress = Backup.findIfBackupsRunningWithCustomerConfig(configUUID);
      if (backupsInProgress) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            "Backup task associated with Configuration "
                + configUUID.toString()
                + " is in progress.");
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
            customerConfig.configName);
        auditService().createAuditEntry(ctx(), request());
        return new YBPTask(taskUUID, configUUID).asResult();
      }
    }
    customerConfigService.delete(customerUUID, configUUID);

    auditService().createAuditEntry(ctx(), request());
    return YBPSuccess.withMessage("Config " + configUUID + " deleted");
  }

  @ApiOperation(
      value = "Delete a customer configuration V2",
      response = YBPTask.class,
      nickname = "deleteCustomerConfigV2")
  public Result deleteYb(UUID customerUUID, UUID configUUID, boolean isDeleteBackups) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    CustomerConfig customerConfig = customerConfigService.getOrBadRequest(customerUUID, configUUID);
    if (customerConfig.type == CustomerConfig.ConfigType.STORAGE) {
      Boolean backupsInProgress = Backup.findIfBackupsRunningWithCustomerConfig(configUUID);
      if (backupsInProgress) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            "Backup task associated with Configuration "
                + configUUID.toString()
                + " is in progress.");
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
          customerConfig.configName);
      auditService().createAuditEntry(ctx(), request());
      return new YBPTask(taskUUID, configUUID).asResult();
    }
    customerConfigService.delete(customerUUID, configUUID);
    auditService().createAuditEntry(ctx(), request());
    return YBPSuccess.withMessage("Config " + configUUID + " is queued for deletion");
  }

  @ApiOperation(
      value = "List all customer configurations",
      response = CustomerConfigUI.class,
      responseContainer = "List",
      nickname = "getListOfCustomerConfig")
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
        dataType = "com.yugabyte.yw.models.CustomerConfig",
        paramType = "body")
  })
  public Result edit(UUID customerUUID, UUID configUUID) {
    CustomerConfig customerConfig = parseJson(CustomerConfig.class);
    customerConfig.setConfigUUID(configUUID);
    customerConfig.setCustomerUUID(customerUUID);

    CustomerConfig existingConfig = customerConfigService.getOrBadRequest(customerUUID, configUUID);
    CustomerConfig unmaskedConfig = CommonUtils.unmaskObject(existingConfig, customerConfig);

    customerConfigService.edit(unmaskedConfig);

    auditService().createAuditEntry(ctx(), request());
    return PlatformResults.withData(unmaskedConfig);
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
        dataType = "com.yugabyte.yw.models.CustomerConfig",
        paramType = "body")
  })
  public Result editYb(UUID customerUUID, UUID configUUID) {

    CustomerConfig existingConfig = customerConfigService.getOrBadRequest(customerUUID, configUUID);
    if (existingConfig.getState().equals(ConfigState.QueuedForDeletion)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot edit config as it is queued for deletion.");
    }

    CustomerConfig customerConfig = parseJson(CustomerConfig.class);
    customerConfig.setConfigUUID(configUUID);
    customerConfig.setCustomerUUID(customerUUID);
    CustomerConfig unmaskedConfig = CommonUtils.unmaskObject(existingConfig, customerConfig);
    customerConfigService.edit(unmaskedConfig);

    auditService().createAuditEntry(ctx(), request());
    return PlatformResults.withData(unmaskedConfig);
  }
}
