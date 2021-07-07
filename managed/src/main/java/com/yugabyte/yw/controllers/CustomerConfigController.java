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
import io.swagger.annotations.*;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.libs.Json;
import play.mvc.Result;

import java.util.UUID;

@Api(
    value = "Customer Config",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class CustomerConfigController extends AuthenticatedController {
  public static final Logger LOG = LoggerFactory.getLogger(CustomerConfigController.class);

  @Inject private CustomerConfigValidator configValidator;

  @ApiOperation(value = "Create customer configuration", response = CustomerConfig.class)
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

    CustomerConfig customerConfig = CustomerConfig.createWithFormData(customerUUID, formData);
    auditService().createAuditEntry(ctx(), request(), formData);
    return YWResults.withData(customerConfig);
  }

  @ApiOperation(value = "Delete customer configuration", response = YWResults.YWSuccess.class)
  public Result delete(UUID customerUUID, UUID configUUID) {
    CustomerConfig customerConfig = CustomerConfig.getOrBadRequest(customerUUID, configUUID);
    customerConfig.deleteOrThrow();
    auditService().createAuditEntry(ctx(), request());
    return YWResults.YWSuccess.withMessage("configUUID deleted");
  }

  @ApiOperation(
      value = "List of customer configuration",
      response = CustomerConfig.class,
      responseContainer = "List")
  public Result list(UUID customerUUID) {
    return YWResults.withData(CustomerConfig.getAll(customerUUID));
  }

  @ApiOperation(value = "List of customer configuration", response = CustomerConfig.class)
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

    errorJson = configValidator.validateDataContent(formData);
    if (errorJson.size() > 0) {
      throw new YWServiceException(BAD_REQUEST, errorJson);
    }
    CustomerConfig config = CustomerConfig.getOrBadRequest(customerUUID, configUUID);
    JsonNode data = Json.toJson(formData.get("data"));
    if (data != null && data.get("BACKUP_LOCATION") != null) {
      ((ObjectNode) data).put("BACKUP_LOCATION", config.data.get("BACKUP_LOCATION"));
    }
    JsonNode updatedData = CommonUtils.unmaskConfig(config.data, data);
    config.data = Json.toJson(updatedData);
    config.configName = formData.get("configName").textValue();
    config.name = formData.get("name").textValue();
    config.update();
    auditService().createAuditEntry(ctx(), request());
    return YWResults.withData(config);
  }
}
