// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.inject.Inject;
import com.yugabyte.yw.common.YWServiceException;
import com.yugabyte.yw.forms.YWResults;
import com.yugabyte.yw.models.CustomerConfig;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.CustomerConfigValidator;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiImplicitParam;
import io.swagger.annotations.ApiImplicitParams;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import java.util.UUID;
import org.apache.commons.lang3.StringUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;
import play.mvc.Result;

@Api(
    value = "Customer Configuration",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class CustomerConfigController extends AuthenticatedController {
  public static final Logger LOG = LoggerFactory.getLogger(CustomerConfigController.class);

  @Inject private CustomerConfigValidator configValidator;

  @ApiOperation(
      value = "Create a customer configuration",
      response = CustomerConfig.class,
      nickname = "createCustomerConfig")
  @ApiImplicitParams({
    @ApiImplicitParam(
        name = "Config",
        value = "Configuration data to be created",
        required = true,
        dataType = "Object",
        paramType = "body")
  })
  public Result create(UUID customerUUID) {
    ObjectNode formData = (ObjectNode) request().body().asJson();
    ObjectNode errorJson = configValidator.validateFormData(formData);
    if (errorJson.size() > 0) {
      throw new YWServiceException(BAD_REQUEST, errorJson);
    }

    errorJson = configValidator.validateDataContent(formData);
    if (errorJson.size() > 0) {
      throw new YWServiceException(BAD_REQUEST, errorJson);
    }

    String configName = formData.get("configName").textValue();
    CustomerConfig existentConfig = CustomerConfig.get(customerUUID, configName);
    if (existentConfig != null) {
      throw new YWServiceException(
          CONFLICT, String.format("Configuration %s already exists", configName));
    }

    CustomerConfig customerConfig = CustomerConfig.createWithFormData(customerUUID, formData);
    auditService().createAuditEntry(ctx(), request(), formData);
    return YWResults.withData(customerConfig);
  }

  @ApiOperation(
      value = "Delete a customer configuration",
      response = YWResults.YWSuccess.class,
      nickname = "deleteCustomerConfig")
  public Result delete(UUID customerUUID, UUID configUUID) {
    CustomerConfig customerConfig = CustomerConfig.getOrBadRequest(customerUUID, configUUID);
    customerConfig.deleteOrThrow();
    auditService().createAuditEntry(ctx(), request());
    return YWResults.YWSuccess.withMessage("configUUID deleted");
  }

  @ApiOperation(
      value = "List all customer configurations",
      response = CustomerConfig.class,
      responseContainer = "List",
      nickname = "getListOfCustomerConfig")
  public Result list(UUID customerUUID) {
    return YWResults.withData(CustomerConfig.getAll(customerUUID));
  }

  @ApiOperation(
      value = "Update a customer configuration",
      response = CustomerConfig.class,
      nickname = "getCustomerConfig")
  @ApiImplicitParams({
    @ApiImplicitParam(
        name = "Config",
        value = "Configuration data to be updated",
        required = true,
        dataType = "Object",
        paramType = "body")
  })
  public Result edit(UUID customerUUID, UUID configUUID) {
    JsonNode formData = request().body().asJson();
    ObjectNode errorJson = configValidator.validateFormData(formData);
    if (errorJson.size() > 0) {
      throw new YWServiceException(BAD_REQUEST, errorJson);
    }

    String configName = formData.get("configName").textValue();
    CustomerConfig existentConfig = CustomerConfig.get(customerUUID, configName);
    if (existentConfig != null
        && !StringUtils.equals(existentConfig.configUUID.toString(), configUUID.toString())) {
      throw new YWServiceException(
          CONFLICT, String.format("Configuration %s already exists", configName));
    }

    CustomerConfig config = CustomerConfig.getOrBadRequest(customerUUID, configUUID);
    JsonNode data = Json.toJson(formData.get("data"));
    if ((data != null) && (data.get("BACKUP_LOCATION") != null)) {
      if (!StringUtils.equals(
          data.get("BACKUP_LOCATION").textValue(),
          config.data.get("BACKUP_LOCATION").textValue())) {
        throw new YWServiceException(BAD_REQUEST, "BACKUP_LOCATION field is read-only.");
      }
    }

    JsonNode updatedData = CommonUtils.unmaskConfig(config.data, data);
    ((ObjectNode) formData).put("data", updatedData);

    errorJson = configValidator.validateDataContent(formData);
    if (errorJson.size() > 0) {
      throw new YWServiceException(BAD_REQUEST, errorJson);
    }

    config.data = Json.toJson(updatedData);
    config.configName = formData.get("configName").textValue();
    config.name = formData.get("name").textValue();
    config.update();
    auditService().createAuditEntry(ctx(), request());
    return YWResults.withData(config);
  }
}
