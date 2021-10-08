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

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.controllers.handlers.XClusterReplicationHandler;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPTask;
import com.yugabyte.yw.forms.XClusterConfigCreateFormData;
import com.yugabyte.yw.forms.XClusterConfigEditFormData;
import com.yugabyte.yw.forms.XClusterConfigTaskParams;
import com.yugabyte.yw.forms.XClusterReplicationFormData;
import com.yugabyte.yw.models.AsyncReplicationRelationship;
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
import java.util.List;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import play.mvc.Result;

@Api(
    value = "XCluster Config",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
@Slf4j
public class XClusterConfigController extends AuthenticatedController {

  private final Commissioner commissioner;

  @Inject
  public XClusterConfigController(Commissioner commissioner) {
    this.commissioner = commissioner;
  }

  @Inject private XClusterReplicationHandler xClusterReplicationHandler;

  /**
   * API that creates an xCluster replication between two universes using the given body parameters
   *
   * @return Result
   */
  @ApiOperation(
      nickname = "create_xcluster",
      value = "Create xCluster Replication",
      response = YBPTask.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "xcluster_replication_form_data",
          value = "XCluster Replication Form Data",
          dataType = "com.yugabyte.yw.forms.XClusterReplicationFormData",
          paramType = "body",
          required = true))
  public Result createOld(UUID customerUUID, UUID targetUniverseUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    XClusterReplicationFormData formData = getFormData();

    validateUniverses(customer, formData.sourceUniverseUUID, targetUniverseUUID);
    validateReplication(formData.sourceUniverseUUID, targetUniverseUUID, false);

    UUID taskUUID =
        xClusterReplicationHandler.createReplication(customer, formData, targetUniverseUUID);

    auditService().createAuditEntryWithReqBody(ctx(), taskUUID);
    return new YBPTask(taskUUID).asResult();
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

    log.info("Submitted create xcluster config {}, task {}", xClusterConfig.uuid, taskUUID);

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
    Customer customer = Customer.getOrBadRequest(customerUUID);
    XClusterConfig xClusterConfig =
        XClusterConfig.getValidConfigOrBadRequest(customer, xclusterConfigUUID);
    return PlatformResults.withData(xClusterConfig);
  }

  /**
   * API that edits an xCluster replication between two universes using the given body parameters
   *
   * @return Result
   */
  @ApiOperation(value = "Edit xCluster Replication", response = YBPTask.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "xcluster_replication_form_data",
          value = "XCluster Replication Form Data",
          dataType = "com.yugabyte.yw.forms.XClusterReplicationFormData",
          paramType = "body",
          required = true))
  public Result editOld(UUID customerUUID, UUID targetUniverseUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    XClusterReplicationFormData formData = getFormData();

    validateUniverses(customer, formData.sourceUniverseUUID, targetUniverseUUID);
    validateReplication(formData.sourceUniverseUUID, targetUniverseUUID, true);

    UUID taskUUID =
        xClusterReplicationHandler.editReplication(customer, formData, targetUniverseUUID);

    auditService().createAuditEntryWithReqBody(ctx(), taskUUID);
    return new YBPTask(taskUUID).asResult();
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

    log.info("Submitted edit xcluster config {}, task {}", xClusterConfig.uuid, taskUUID);

    auditService().createAuditEntryWithReqBody(ctx(), taskUUID);
    return new YBPTask(taskUUID, xClusterConfig.uuid).asResult();
  }

  /**
   * API that deletes an xCluster replication between two universes using the given body parameters
   *
   * @return Result
   */
  @ApiOperation(
      nickname = "delete_xcluster",
      value = "Delete xCluster Replication",
      response = YBPTask.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "xcluster_replication_form_data",
          value = "XCluster Replication Form Data",
          dataType = "com.yugabyte.yw.forms.XClusterReplicationFormData",
          paramType = "body",
          required = true))
  public Result deleteOld(UUID customerUUID, UUID targetUniverseUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    XClusterReplicationFormData formData = getFormData();

    validateUniverses(customer, formData.sourceUniverseUUID, targetUniverseUUID);
    validateReplication(formData.sourceUniverseUUID, targetUniverseUUID, true);

    UUID taskUUID =
        xClusterReplicationHandler.deleteReplication(customer, formData, targetUniverseUUID);

    auditService().createAuditEntryWithReqBody(ctx(), taskUUID);
    return new YBPTask(taskUUID).asResult();
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

    log.info("Submitted delete xcluster config {}, task {}", xClusterConfig.uuid, taskUUID);

    auditService().createAuditEntryWithReqBody(ctx(), taskUUID);
    return new YBPTask(taskUUID).asResult();
  }

  /**
   * API that pauses an xCluster replication between two universes using the given body parameters
   *
   * @return Result
   */
  @ApiOperation(value = "Pause xCluster Replication", response = YBPTask.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "xcluster_replication_form_data",
          value = "XCluster Replication Form Data",
          dataType = "com.yugabyte.yw.forms.XClusterReplicationFormData",
          paramType = "body",
          required = true))
  public Result pauseOld(UUID customerUUID, UUID targetUniverseUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    XClusterReplicationFormData formData = getFormData();

    validateUniverses(customer, formData.sourceUniverseUUID, targetUniverseUUID);
    validateReplication(formData.sourceUniverseUUID, targetUniverseUUID, true);

    UUID taskUUID =
        xClusterReplicationHandler.pauseReplication(customer, formData, targetUniverseUUID);

    auditService().createAuditEntryWithReqBody(ctx(), taskUUID);
    return new YBPTask(taskUUID).asResult();
  }

  /**
   * API that resumes an xCluster replication between two universes using the given body parameters
   *
   * @return Result
   */
  @ApiOperation(value = "Resume xCluster Replication", response = YBPTask.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "xcluster_replication_form_data",
          value = "XCluster Replication Form Data",
          dataType = "com.yugabyte.yw.forms.XClusterReplicationFormData",
          paramType = "body",
          required = true))
  public Result resumeOld(UUID customerUUID, UUID targetUniverseUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    XClusterReplicationFormData formData = getFormData();

    validateUniverses(customer, formData.sourceUniverseUUID, targetUniverseUUID);
    validateReplication(formData.sourceUniverseUUID, targetUniverseUUID, true);

    UUID taskUUID =
        xClusterReplicationHandler.resumeReplication(customer, formData, targetUniverseUUID);

    auditService().createAuditEntryWithReqBody(ctx(), taskUUID);
    return new YBPTask(taskUUID).asResult();
  }

  private XClusterReplicationFormData getFormData() {
    ObjectMapper mapper = new ObjectMapper();
    XClusterReplicationFormData formData;

    try {
      formData = mapper.treeToValue(request().body().asJson(), XClusterReplicationFormData.class);
    } catch (RuntimeException | JsonProcessingException e) {
      throw new PlatformServiceException(BAD_REQUEST, "Invalid JSON");
    }

    return formData;
  }

  private XClusterConfigCreateFormData parseCreateFormData() {
    ObjectMapper mapper = new ObjectMapper();
    XClusterConfigCreateFormData formData;

    try {
      formData = mapper.treeToValue(request().body().asJson(), XClusterConfigCreateFormData.class);
    } catch (RuntimeException | JsonProcessingException e) {
      throw new PlatformServiceException(BAD_REQUEST, "Invalid JSON: " + e);
    }

    return formData;
  }

  private XClusterConfigEditFormData parseEditFormData() {
    ObjectMapper mapper = new ObjectMapper();
    XClusterConfigEditFormData formData;

    try {
      formData = mapper.treeToValue(request().body().asJson(), XClusterConfigEditFormData.class);
    } catch (RuntimeException | JsonProcessingException e) {
      throw new PlatformServiceException(BAD_REQUEST, "Invalid JSON: " + e);
    }

    return formData;
  }

  private void validateUniverses(
      Customer customer, UUID sourceUniverseUUID, UUID targetUniverseUUID) {
    // check if universes exist and if they belong to customer
    Universe.getValidUniverseOrBadRequest(targetUniverseUUID, customer);
    Universe.getValidUniverseOrBadRequest(sourceUniverseUUID, customer);
  }

  private void validateReplication(
      UUID sourceUniverseUUID, UUID targetUniverseUUID, boolean replicationShouldExist) {
    // check if replication specified in form exists or not (based on replicationShouldExist)
    List<AsyncReplicationRelationship> results =
        AsyncReplicationRelationship.getBetweenUniverses(sourceUniverseUUID, targetUniverseUUID);

    if (results.isEmpty() == replicationShouldExist) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "xCluster replication between source universe "
              + sourceUniverseUUID
              + " and target universe "
              + targetUniverseUUID
              + " was"
              + (replicationShouldExist ? " not" : " already")
              + " found");
    }
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
