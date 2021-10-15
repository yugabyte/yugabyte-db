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

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPTask;
import com.yugabyte.yw.forms.XClusterConfigCreateFormData;
import com.yugabyte.yw.forms.XClusterConfigEditFormData;
import com.yugabyte.yw.forms.XClusterConfigTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.helpers.TaskType;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiImplicitParam;
import io.swagger.annotations.ApiImplicitParams;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import java.util.HashSet;
import java.util.List;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import play.mvc.Result;

@Api(
    value = "XClusterConfig",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH),
    hidden = true)
@Slf4j
public class XClusterConfigController extends AuthenticatedController {

  private final Commissioner commissioner;

  @Inject
  public XClusterConfigController(Commissioner commissioner) {
    this.commissioner = commissioner;
  }

  /**
   * API that creates an xCluster replication configuration.
   *
   * @return Result
   */
  @ApiOperation(
      nickname = "createXClusterConfig",
      value = "Create xcluster config",
      response = YBPTask.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "xcluster_replication_create_form_data",
          value = "XCluster Replication Create Form Data",
          dataType = "com.yugabyte.yw.forms.XClusterConfigCreateFormData",
          paramType = "body",
          required = true))
  public Result create(UUID customerUUID) {
    log.info("Received create XClusterConfig request");

    // Parse and validate request
    Customer customer = Customer.getOrBadRequest(customerUUID);
    XClusterConfigCreateFormData createFormData = parseCreateFormData();
    Universe.getValidUniverseOrBadRequest(createFormData.sourceUniverseUUID, customer);
    Universe.getValidUniverseOrBadRequest(createFormData.targetUniverseUUID, customer);
    checkConfigDoesNotAlreadyExist(
        createFormData.sourceUniverseUUID, createFormData.targetUniverseUUID);

    // Create xCluster config object
    XClusterConfig xClusterConfig = XClusterConfig.create(createFormData);

    // Submit task to set up xCluster config
    XClusterConfigTaskParams taskParams =
        new XClusterConfigTaskParams(xClusterConfig, createFormData);
    UUID taskUUID = commissioner.submit(TaskType.CreateXClusterConfig, taskParams);
    CustomerTask.create(
        customer,
        xClusterConfig.uuid,
        taskUUID,
        CustomerTask.TargetType.XClusterConfig,
        CustomerTask.TaskType.CreateXClusterConfig,
        xClusterConfig.name);

    log.info("Submitted create XClusterConfig({}), task {}", xClusterConfig.uuid, taskUUID);

    auditService().createAuditEntryWithReqBody(ctx(), taskUUID);
    return new YBPTask(taskUUID, xClusterConfig.uuid).asResult();
  }

  /**
   * API that gets an xCluster replication configuration.
   *
   * @return Result
   */
  @ApiOperation(
      nickname = "getXClusterConfig",
      value = "Get xcluster config",
      response = XClusterConfig.class)
  public Result get(UUID customerUUID, UUID xclusterConfigUUID) {
    log.info("Received get XClusterConfig({}) request", xclusterConfigUUID);
    Customer customer = Customer.getOrBadRequest(customerUUID);
    XClusterConfig xClusterConfig =
        XClusterConfig.getValidConfigOrBadRequest(customer, xclusterConfigUUID);
    return PlatformResults.withData(xClusterConfig);
  }

  /**
   * API that edits an xCluster replication configuration.
   *
   * @return Result
   */
  @ApiOperation(
      nickname = "editXClusterConfig",
      value = "Edit xcluster config",
      response = YBPTask.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "xcluster_replication_edit_form_data",
          value = "XCluster Replication Edit Form Data",
          dataType = "com.yugabyte.yw.forms.XClusterConfigEditFormData",
          paramType = "body",
          required = true))
  public Result edit(UUID customerUUID, UUID xclusterConfigUUID) {
    log.info("Received edit XClusterConfig({}) request", xclusterConfigUUID);

    // Parse and validate request
    Customer customer = Customer.getOrBadRequest(customerUUID);
    XClusterConfigEditFormData editFormData = parseEditFormData();
    XClusterConfig xClusterConfig =
        XClusterConfig.getValidConfigOrBadRequest(customer, xclusterConfigUUID);

    // Submit task to edit xCluster config
    XClusterConfigTaskParams params = new XClusterConfigTaskParams(xClusterConfig, editFormData);
    UUID taskUUID = commissioner.submit(TaskType.EditXClusterConfig, params);
    CustomerTask.create(
        customer,
        xClusterConfig.uuid,
        taskUUID,
        CustomerTask.TargetType.XClusterConfig,
        CustomerTask.TaskType.EditXClusterConfig,
        xClusterConfig.name);

    log.info("Submitted edit XClusterConfig({}), task {}", xClusterConfig.uuid, taskUUID);

    auditService().createAuditEntryWithReqBody(ctx(), taskUUID);
    return new YBPTask(taskUUID, xClusterConfig.uuid).asResult();
  }

  /**
   * API that deletes an xCluster replication configuration.
   *
   * @return Result
   */
  @ApiOperation(
      nickname = "deleteXClusterConfig",
      value = "Delete xcluster config",
      response = YBPTask.class)
  public Result delete(UUID customerUUID, UUID xclusterConfigUUID) {
    log.info("Received delete XClusterConfig({}) request", xclusterConfigUUID);

    // Parse and validate request
    Customer customer = Customer.getOrBadRequest(customerUUID);
    XClusterConfig xClusterConfig =
        XClusterConfig.getValidConfigOrBadRequest(customer, xclusterConfigUUID);

    // Submit task to delete xCluster config
    XClusterConfigTaskParams params = new XClusterConfigTaskParams(xClusterConfig);
    UUID taskUUID = commissioner.submit(TaskType.DeleteXClusterConfig, params);
    CustomerTask.create(
        customer,
        xclusterConfigUUID,
        taskUUID,
        CustomerTask.TargetType.XClusterConfig,
        CustomerTask.TaskType.DeleteXClusterConfig,
        xClusterConfig.name);

    log.info("Submitted delete XClusterConfig({}), task {}", xClusterConfig.uuid, taskUUID);

    auditService().createAuditEntryWithReqBody(ctx(), taskUUID);
    return new YBPTask(taskUUID).asResult();
  }

  private XClusterConfigCreateFormData parseCreateFormData() {
    XClusterConfigCreateFormData formData =
        formFactory.getFormDataOrBadRequest(
            request().body().asJson(), XClusterConfigCreateFormData.class);

    // TODO: Make a custom validation constraint for this.
    if (formData.tables != null && formData.tables.size() == 0) {
      throw new PlatformServiceException(BAD_REQUEST, "Table set must be non-empty");
    }

    // Set default value for bootstrapIds
    if (formData.bootstrapIds == null) {
      formData.bootstrapIds = new HashSet<>();
    }

    return formData;
  }

  private XClusterConfigEditFormData parseEditFormData() {
    XClusterConfigEditFormData formData =
        formFactory.getFormDataOrBadRequest(
            request().body().asJson(), XClusterConfigEditFormData.class);

    // Ensure exactly one edit form field is specified
    // TODO: There must be a better way to do this.
    int numEditOps = 0;
    numEditOps += (formData.name != null) ? 1 : 0;
    numEditOps += (formData.status != null) ? 1 : 0;
    numEditOps += (formData.tables != null) ? 1 : 0;
    if (numEditOps == 0) {
      throw new PlatformServiceException(BAD_REQUEST, "Must specify an edit operation");
    } else if (numEditOps > 1) {
      throw new PlatformServiceException(BAD_REQUEST, "Must perform one edit operation at a time");
    }

    // TODO: Make a custom validation constraint for this.
    if (formData.tables != null && formData.tables.size() == 0) {
      throw new PlatformServiceException(BAD_REQUEST, "Table set must be non-empty");
    }

    return formData;
  }

  private void checkConfigDoesNotAlreadyExist(UUID sourceUniverseUUID, UUID targetUniverseUUID) {
    // check if config specified in form exists or not (based on shouldExist)
    List<XClusterConfig> results =
        XClusterConfig.getBetweenUniverses(sourceUniverseUUID, targetUniverseUUID);

    if (!results.isEmpty()) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "xCluster config between source universe "
              + sourceUniverseUUID
              + " and target universe "
              + targetUniverseUUID
              + " already exists.");
    }
  }
}
