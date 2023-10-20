// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import com.google.inject.Inject;
import com.yugabyte.yw.controllers.handlers.YbcHandler;
import com.yugabyte.yw.forms.PlatformResults.YBPTask;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.common.YbaApi.YbaApiVisibility;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import java.util.UUID;
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
}
