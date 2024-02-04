// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.databind.DeserializationFeature;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.SerializationFeature;
import com.google.inject.Inject;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.controllers.handlers.YbcHandler;
import com.yugabyte.yw.forms.PlatformResults.YBPTask;
import com.yugabyte.yw.forms.YbcGflagsTaskParams;
import com.yugabyte.yw.forms.ybc.YbcGflags;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.common.YbaApi.YbaApiVisibility;
import com.yugabyte.yw.rbac.annotations.AuthzPath;
import com.yugabyte.yw.rbac.annotations.PermissionAttribute;
import com.yugabyte.yw.rbac.annotations.RequiredPermissionOnResource;
import com.yugabyte.yw.rbac.annotations.Resource;
import com.yugabyte.yw.rbac.enums.SourceType;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import java.util.UUID;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Result;

@Api(
    value = "Ybc Management",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class YbcController extends AuthenticatedController {

  @Inject private YbcHandler ybcHandler;

  /**
   * API that disables the yb-controller on a universe.
   *
   * @param customerUUID
   * @param universeUUID
   * @return Result with disable ybc operation with task id
   */
  @YbaApi(visibility = YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.16.0.0")
  @ApiOperation(
      value = "YbaApi Internal. Disable YBC on the universe nodes",
      nickname = "disableYbc",
      response = YBPTask.class)
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  public Result disable(UUID customerUUID, UUID universeUUID, Http.Request request) {
    UUID taskUUID = ybcHandler.disable(customerUUID, universeUUID);
    auditService()
        .createAuditEntry(
            request,
            Audit.TargetType.Universe,
            universeUUID.toString(),
            Audit.ActionType.DisableYbc);
    return new YBPTask(taskUUID).asResult();
  }

  /**
   * API that upgrades the existing ybc on a universe.
   *
   * @param customerUUID
   * @param universeUUID
   * @param ybcVersion
   * @return Result with upgrade ybc operation with task id
   */
  @YbaApi(visibility = YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.16.0.0")
  @ApiOperation(
      value = "YbaApi Internal. Upgrade YBC on the universe nodes",
      nickname = "upgradeYbc",
      response = YBPTask.class)
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  public Result upgrade(
      UUID customerUUID, UUID universeUUID, String ybcVersion, Http.Request request) {
    UUID taskUUID = ybcHandler.upgrade(customerUUID, universeUUID, ybcVersion);
    auditService()
        .createAuditEntry(
            request,
            Audit.TargetType.Universe,
            universeUUID.toString(),
            Audit.ActionType.UpgradeYbc);
    return new YBPTask(taskUUID).asResult();
  }

  /**
   * API that install ybc on a non-ybc enabled universe.
   *
   * @param customerUUID
   * @param universeUUID
   * @param ybcVersion
   * @return Result with install ybc operation with task id
   */
  @YbaApi(visibility = YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.16.0.0")
  @ApiOperation(
      value = "YbaApi Internal. Install YBC on the universe nodes",
      nickname = "installYbc",
      response = YBPTask.class)
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  public Result install(
      UUID customerUUID, UUID universeUUID, String ybcVersion, Http.Request request) {
    UUID taskUUID = ybcHandler.install(customerUUID, universeUUID, ybcVersion);
    auditService()
        .createAuditEntry(
            request,
            Audit.TargetType.Universe,
            universeUUID.toString(),
            Audit.ActionType.InstallYbc);
    return new YBPTask(taskUUID).asResult();
  }

  /**
   * API that upgrades ybc gflags
   *
   * @param customerUUID
   * @param universeUUID
   * @param request
   * @return Result of ybc gflags upgrade with task id
   */
  @YbaApi(visibility = YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.21.0.0")
  @ApiOperation(
      value = "YbaApi Internal. Upgrade YBC gflags on the universe nodes",
      nickname = "upgradeYbcGflags",
      response = YBPTask.class)
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  public Result upgradeYbcGflags(UUID customerUUID, UUID universeUUID, Http.Request request) {

    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID, customer);

    if (!universe.isYbcEnabled()) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Can perform this operation only on a YBC universe");
    }
    // Parse request body.
    JsonNode requestBody = request.body().asJson();
    ObjectMapper mapper =
        Json.mapper()
            .copy()
            .configure(DeserializationFeature.FAIL_ON_UNKNOWN_PROPERTIES, false)
            .configure(SerializationFeature.FAIL_ON_EMPTY_BEANS, false);
    YbcGflags ybcGflags = new YbcGflags();
    try {
      ybcGflags = mapper.readValue(mapper.writeValueAsString(requestBody), YbcGflags.class);
    } catch (Exception e) {
      throw new PlatformServiceException(BAD_REQUEST, requestBody);
    }

    YbcGflagsTaskParams taskParams = new YbcGflagsTaskParams();
    taskParams.setUniverseUUID(universeUUID);
    taskParams.customerUUID = customerUUID;
    taskParams.ybcGflags = ybcGflags;
    taskParams.clusters = universe.getUniverseDetails().clusters;

    UUID taskUUID = ybcHandler.upgradeYbcGflags(customerUUID, universeUUID, taskParams);
    auditService()
        .createAuditEntry(
            request,
            Audit.TargetType.Universe,
            universeUUID.toString(),
            Audit.ActionType.UpgradeYbcGFlags);
    return new YBPTask(taskUUID).asResult();
  }
}
