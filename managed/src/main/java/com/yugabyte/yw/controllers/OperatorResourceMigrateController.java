package com.yugabyte.yw.controllers;

import com.google.inject.Inject;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.controllers.handlers.OperatorResourceMigrateHandler;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.forms.PlatformResults.YBPTask;
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
import lombok.extern.slf4j.Slf4j;
import play.mvc.Result;

@Api(
    value = "Import YBA resources to be managed by the kubernetes operator",
    tags = "preview",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
@Slf4j
public class OperatorResourceMigrateController extends AuthenticatedController {

  @Inject private OperatorResourceMigrateHandler handler;

  @ApiOperation(
      value = "Import a universe and related resources to be managed by the kubernetes operator",
      nickname = "operatorImportUniverse",
      response = YBPTask.class,
      notes = "WARNING: This is a preview API that could change: import operator universe")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2025.2.0.0")
  public Result importUniverse(UUID customerUUID, UUID universeUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID, customer);

    // Run prechecks to ensure the universe can be migrated
    log.info("Prechecking universe {} for import to operator", universe.getName());
    handler.precheckUniverseImport(universe);
    log.debug("Creating task to migrate universe {} to operator", universe.getName());
    UUID taskUUID = handler.migrateUniverseToOperator(universe);
    log.debug("Created task {} to migrate universe {} to operator", taskUUID, universe.getName());

    return new YBPTask(taskUUID).asResult();
  }

  @ApiOperation(
      value = "precheck that the universe can be imported to the operator",
      nickname = "operatorPrecheckImportUniverse",
      response = YBPSuccess.class,
      notes = "WARNING: This is a preview API that could change: precheck operator universe import")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2025.2.0.0")
  public Result precheckImportUniverse(UUID customerUUID, UUID universeUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID, customer);

    log.info("Prechecking universe {} for import to operator", universe.getName());
    // Run prechecks to ensure the universe can be migrated
    handler.precheckUniverseImport(universe);
    log.info("Prechecks for import to operator have succeeded");
    return YBPSuccess.empty();
  }
}
