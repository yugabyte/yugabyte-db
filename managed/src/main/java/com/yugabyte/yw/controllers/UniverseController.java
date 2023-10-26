// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import com.google.inject.Inject;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.common.rbac.RoleBindingUtil;
import com.yugabyte.yw.controllers.handlers.UniverseCRUDHandler;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPTask;
import com.yugabyte.yw.forms.UniverseResp;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.extended.UserWithFeatures;
import com.yugabyte.yw.rbac.annotations.AuthzPath;
import com.yugabyte.yw.rbac.annotations.PermissionAttribute;
import com.yugabyte.yw.rbac.annotations.RequiredPermissionOnResource;
import com.yugabyte.yw.rbac.annotations.Resource;
import com.yugabyte.yw.rbac.enums.SourceType;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import java.util.ArrayList;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.mvc.Http;
import play.mvc.Result;

@Api(
    value = "Universe management",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class UniverseController extends AuthenticatedController {
  private static final Logger LOG = LoggerFactory.getLogger(UniverseController.class);

  @Inject private RuntimeConfigFactory runtimeConfigFactory;
  @Inject private UniverseCRUDHandler universeCRUDHandler;
  @Inject private RoleBindingUtil roleBindingUtil;

  /** List the universes for a given customer. */
  @ApiOperation(
      value = "Available since YBA version 2.2.0.0. List universes",
      response = UniverseResp.class,
      responseContainer = "List",
      nickname = "listUniverses")
  @YbaApi(visibility = YbaApi.YbaApiVisibility.PUBLIC, sinceYBAVersion = "2.2.0.0")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.READ),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT),
        checkOnlyPermission = true)
  })
  public Result list(UUID customerUUID, String name) {
    UserWithFeatures user = RequestContext.get(TokenAuthenticator.USER);
    Set<UUID> resourceUUIDs =
        roleBindingUtil.getResourceUuids(
            user.getUser().getUuid(), ResourceType.UNIVERSE, Action.READ);
    Customer customer = Customer.getOrBadRequest(customerUUID);
    // Verify the customer is present.
    List<UniverseResp> universeRespList = new ArrayList<>();
    if (name != null) {
      LOG.info("Finding Universe with name {}.", name);
      universeRespList = universeCRUDHandler.findByName(customer, name);
    } else {
      universeRespList = universeCRUDHandler.list(customer);
    }
    universeRespList =
        universeRespList.stream()
            .filter(u -> resourceUUIDs.contains(u.universeUUID))
            .collect(Collectors.toList());
    return PlatformResults.withData(universeRespList);
  }

  @ApiOperation(
      value = "Available since YBA version 2.2.0.0. Get a universe",
      response = UniverseResp.class,
      nickname = "getUniverse")
  @YbaApi(visibility = YbaApi.YbaApiVisibility.PUBLIC, sinceYBAVersion = "2.2.0.0")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.READ),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  public Result index(UUID customerUUID, UUID universeUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID, customer);
    return PlatformResults.withData(
        UniverseResp.create(universe, null, runtimeConfigFactory.globalRuntimeConf()));
  }

  @ApiOperation(
      value = "Available since YBA version 2.2.0.0. Delete a universe",
      response = YBPTask.class,
      nickname = "deleteUniverse")
  @YbaApi(visibility = YbaApi.YbaApiVisibility.PUBLIC, sinceYBAVersion = "2.2.0.0")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.DELETE),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  public Result destroy(
      UUID customerUUID,
      UUID universeUUID,
      boolean isForceDelete,
      boolean isDeleteBackups,
      boolean isDeleteAssociatedCerts,
      Http.Request request) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID, customer);

    UUID taskUUID =
        universeCRUDHandler.destroy(
            customer, universe, isForceDelete, isDeleteBackups, isDeleteAssociatedCerts);
    auditService()
        .createAuditEntry(
            request,
            Audit.TargetType.Universe,
            universeUUID.toString(),
            Audit.ActionType.Delete,
            taskUUID);
    return new YBPTask(taskUUID, universe.getUniverseUUID()).asResult();
  }
}
