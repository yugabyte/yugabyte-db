// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import com.yugabyte.yw.common.customer.config.CustomerConfigService;
import com.yugabyte.yw.common.customer.config.CustomerConfigUI;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.models.CustomerConfig;
import com.yugabyte.yw.models.helpers.CommonUtils;
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
      response = YBPSuccess.class,
      nickname = "deleteCustomerConfig")
  public Result delete(UUID customerUUID, UUID configUUID) {
    customerConfigService.delete(customerUUID, configUUID);

    auditService().createAuditEntry(ctx(), request());
    return YBPSuccess.withMessage("Config " + configUUID + " deleted");
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
}
