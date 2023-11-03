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

import static com.yugabyte.yw.controllers.UniverseControllerRequestBinder.bindFormDataToTaskParams;

import com.google.inject.Inject;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.operator.annotations.BlockOperatorResource;
import com.yugabyte.yw.common.operator.annotations.OperatorResourceTypes;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.controllers.handlers.UniverseCRUDHandler;
import com.yugabyte.yw.forms.PlatformResults.YBPTask;
import com.yugabyte.yw.forms.UniverseConfigureTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.forms.UniverseResp;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
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
import play.mvc.Http;
import play.mvc.Result;

@Api(
    value = "UniverseClusterMutations",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class UniverseClustersController extends AuthenticatedController {

  @Inject private UniverseCRUDHandler universeCRUDHandler;

  @ApiOperation(
      value = "Create Universe Clusters",
      notes =
          "This will configure and create universe with (optionally) multiple clusters. "
              + "Just fill in the userIntent for PRIMARY and (optionally) an ASYNC cluster",
      response = YBPTask.class,
      nickname = "createAllClusters")
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "UniverseConfigureTaskParams",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.UniverseConfigureTaskParams",
          required = true))
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.CREATE),
        resourceLocation =
            @Resource(path = Util.UNIVERSE_UUID, sourceType = SourceType.REQUEST_BODY))
  })
  @BlockOperatorResource(resource = OperatorResourceTypes.UNIVERSE)
  public Result createAllClusters(UUID customerUUID, Http.Request request) {
    // TODO: add assertions that only expected params are set or bad_request
    // Basically taskParams.clusters[]->userIntent and may be few more things
    Customer customer = Customer.getOrBadRequest(customerUUID);

    UniverseConfigureTaskParams taskParams =
        bindFormDataToTaskParams(request, UniverseConfigureTaskParams.class);

    // explicitly setting useSystemd as true while creating new universe cluster if this property
    // is not already set by caller.
    if (taskParams.getPrimaryCluster().userIntent.useSystemd == null) {
      taskParams.getPrimaryCluster().userIntent.useSystemd = true;
    }

    taskParams.clusterOperation = UniverseConfigureTaskParams.ClusterOperationType.CREATE;
    taskParams.currentClusterType = ClusterType.PRIMARY;
    universeCRUDHandler.configure(customer, taskParams);

    if (taskParams.clusters.stream()
        .anyMatch(cluster -> cluster.clusterType == ClusterType.ASYNC)) {
      taskParams.currentClusterType = ClusterType.ASYNC;
      universeCRUDHandler.configure(customer, taskParams);
    }
    UniverseResp universeResp = universeCRUDHandler.createUniverse(customer, taskParams);
    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.Universe,
            Objects.toString(universeResp.universeUUID, null),
            Audit.ActionType.CreateCluster,
            universeResp.taskUUID);

    return new YBPTask(universeResp.taskUUID, universeResp.universeUUID).asResult();
  }

  /** Takes UDTParams and update universe. Just fill in the userIntent for PRIMARY cluster. */
  @ApiOperation(
      value = "Update Primary Cluster",
      notes =
          "This will update primary cluster of existing universe."
              + "Use API to GET current universe. Lookup universeDetails attribute of the universe "
              + "resource returned. Update the necessary field (e.g. numNodes) Use this "
              + "updated universeDetails as request body. See https://github.com/yugabyte/"
              + "yugabyte-db/blob/master/managed/api-examples/python-simple/edit-universe.ipynb",
      response = YBPTask.class,
      nickname = "updatePrimaryCluster")
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "UniverseConfigureTaskParams",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.UniverseConfigureTaskParams",
          required = true))
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  @BlockOperatorResource(resource = OperatorResourceTypes.UNIVERSE)
  public Result updatePrimaryCluster(UUID customerUUID, UUID universeUUID, Http.Request request) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID, customer);
    UniverseConfigureTaskParams taskParams =
        bindFormDataToTaskParams(request, UniverseConfigureTaskParams.class);

    taskParams.clusterOperation = UniverseConfigureTaskParams.ClusterOperationType.EDIT;
    taskParams.currentClusterType = ClusterType.PRIMARY;
    universeCRUDHandler.configure(customer, taskParams);

    UUID taskUUID = universeCRUDHandler.update(customer, universe, taskParams);
    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.Universe,
            universeUUID.toString(),
            Audit.ActionType.UpdatePrimaryCluster,
            taskUUID);
    return new YBPTask(taskUUID, universeUUID).asResult();
  }

  @ApiOperation(
      value = "Create ReadOnly Cluster",
      notes =
          "This will add a readonly cluster to existing universe. "
              + "Just fill in the userIntent for ASYNC cluster.",
      response = YBPTask.class,
      nickname = "createReadOnlyCluster")
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "UniverseConfigureTaskParams",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.UniverseDefinitionTaskParams",
          required = true))
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  public Result createReadOnlyCluster(UUID customerUUID, UUID universeUUID, Http.Request request) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID, customer);
    UniverseConfigureTaskParams taskParams =
        bindFormDataToTaskParams(request, UniverseConfigureTaskParams.class);
    taskParams.clusterOperation = UniverseConfigureTaskParams.ClusterOperationType.CREATE;
    taskParams.currentClusterType = ClusterType.ASYNC;
    universeCRUDHandler.configure(customer, taskParams);

    UUID taskUUID = universeCRUDHandler.createCluster(customer, universe, taskParams);

    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.Universe,
            universeUUID.toString(),
            Audit.ActionType.CreateReadOnlyCluster,
            taskUUID);
    return new YBPTask(taskUUID, universeUUID).asResult();
  }

  @ApiOperation(
      value = "Delete Readonly Cluster",
      notes = "This will delete readonly cluster of existing universe.",
      response = YBPTask.class,
      nickname = "deleteReadonlyCluster")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  @BlockOperatorResource(resource = OperatorResourceTypes.UNIVERSE)
  public Result deleteReadonlyCluster(
      UUID customerUUID,
      UUID universeUUID,
      UUID clusterUUID,
      Boolean isForceDelete,
      Http.Request request) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID, customer);

    UUID taskUUID =
        universeCRUDHandler.clusterDelete(customer, universe, clusterUUID, isForceDelete);

    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.Universe,
            universeUUID.toString(),
            Audit.ActionType.DeleteReadOnlyCluster,
            taskUUID);
    return new YBPTask(taskUUID, universeUUID).asResult();
  }

  /**
   * Takes UDTParams and update universe. Just fill in the userIntent for either PRIMARY or ASYNC
   * cluster. Only one cluster can be updated at a time.
   */
  @ApiOperation(
      value = "Update Readonly Cluster",
      notes =
          "This will update readonly cluster of existing universe."
              + "Use API to GET current universe. Lookup universeDetails attribute of the universe "
              + "resource returned. Update the necessary field (e.g. numNodes) Use this "
              + "updated universeDetails as request body. See https://github.com/yugabyte/"
              + "yugabyte-db/blob/master/managed/api-examples/python-simple/edit-universe.ipynb",
      response = YBPTask.class,
      nickname = "updateReadOnlyCluster")
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "UniverseConfigureTaskParams",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.UniverseConfigureTaskParams",
          required = true))
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  @BlockOperatorResource(resource = OperatorResourceTypes.UNIVERSE)
  public Result updateReadOnlyCluster(UUID customerUUID, UUID universeUUID, Http.Request request) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID, customer);
    UniverseConfigureTaskParams taskParams =
        bindFormDataToTaskParams(request, UniverseConfigureTaskParams.class);

    taskParams.clusterOperation = UniverseConfigureTaskParams.ClusterOperationType.EDIT;
    taskParams.currentClusterType = ClusterType.ASYNC;
    universeCRUDHandler.configure(customer, taskParams);

    UUID taskUUID = universeCRUDHandler.update(customer, universe, taskParams);
    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.Universe,
            universeUUID.toString(),
            Audit.ActionType.UpdateReadOnlyCluster,
            taskUUID);
    return new YBPTask(taskUUID, universeUUID).asResult();
  }
}
