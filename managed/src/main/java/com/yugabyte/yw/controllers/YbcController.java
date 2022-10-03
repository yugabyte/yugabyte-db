// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import java.util.UUID;

import com.google.inject.Inject;
import com.yugabyte.yw.controllers.handlers.YbcHandler;
import com.yugabyte.yw.forms.PlatformResults.YBPTask;
import com.yugabyte.yw.models.Audit;
import io.swagger.annotations.Api;
import io.swagger.annotations.Authorization;
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
  public Result disable(UUID customerUUID, UUID universeUUID) {
    UUID taskUUID = ybcHandler.disable(customerUUID, universeUUID);
    auditService()
        .createAuditEntryWithReqBody(
            ctx(), Audit.TargetType.Universe, universeUUID.toString(), Audit.ActionType.DisableYbc);
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
  public Result upgrade(UUID customerUUID, UUID universeUUID, String ybcVersion) {
    UUID taskUUID = ybcHandler.upgrade(customerUUID, universeUUID, ybcVersion);
    auditService()
        .createAuditEntryWithReqBody(
            ctx(), Audit.TargetType.Universe, universeUUID.toString(), Audit.ActionType.UpgradeYbc);
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
  public Result install(UUID customerUUID, UUID universeUUID, String ybcVersion) {
    UUID taskUUID = ybcHandler.install(customerUUID, universeUUID, ybcVersion);
    auditService()
        .createAuditEntryWithReqBody(
            ctx(), Audit.TargetType.Universe, universeUUID.toString(), Audit.ActionType.InstallYbc);
    return new YBPTask(taskUUID).asResult();
  }
}
