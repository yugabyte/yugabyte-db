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
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.controllers.handlers.XClusterReplicationHandler;
import com.yugabyte.yw.forms.XClusterReplicationFormData;
import com.yugabyte.yw.forms.PlatformResults.YBPTask;
import com.yugabyte.yw.models.AsyncReplicationRelationship;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
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
    value = "XCluster Replication",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
@Slf4j
public class XClusterReplicationController extends AuthenticatedController {

  @Inject private XClusterReplicationHandler xClusterReplicationHandler;

  /**
   * API that creates an xCluster replication between two universes using the given body parameters
   *
   * @return Result
   */
  @ApiOperation(value = "Create xCluster Replication", response = YBPTask.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "xcluster_replication_form_data",
          value = "XCluster Replication Form Data",
          dataType = "com.yugabyte.yw.forms.XClusterReplicationFormData",
          paramType = "body",
          required = true))
  public Result create(UUID customerUUID, UUID targetUniverseUUID) {
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
  public Result edit(UUID customerUUID, UUID targetUniverseUUID) {
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
   * API that deletes an xCluster replication between two universes using the given body parameters
   *
   * @return Result
   */
  @ApiOperation(value = "Delete xCluster Replication", response = YBPTask.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "xcluster_replication_form_data",
          value = "XCluster Replication Form Data",
          dataType = "com.yugabyte.yw.forms.XClusterReplicationFormData",
          paramType = "body",
          required = true))
  public Result delete(UUID customerUUID, UUID targetUniverseUUID) {
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
  public Result pause(UUID customerUUID, UUID targetUniverseUUID) {
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
  public Result resume(UUID customerUUID, UUID targetUniverseUUID) {
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
}
