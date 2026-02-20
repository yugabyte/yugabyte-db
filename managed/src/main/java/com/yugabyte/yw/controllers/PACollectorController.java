// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.controllers;

import com.google.inject.Inject;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.pa.PerfAdvisorService;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.forms.PACollectorExt;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Audit.ActionType;
import com.yugabyte.yw.models.Audit.TargetType;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.PACollector;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.filters.PACollectorFilter;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.rbac.annotations.AuthzPath;
import com.yugabyte.yw.rbac.annotations.PermissionAttribute;
import com.yugabyte.yw.rbac.annotations.RequiredPermissionOnResource;
import com.yugabyte.yw.rbac.annotations.Resource;
import com.yugabyte.yw.rbac.enums.SourceType;
import io.swagger.annotations.*;
import java.util.List;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import play.mvc.Http;
import play.mvc.Result;

@Api(
    value = "PA Collector",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
@Slf4j
public class PACollectorController extends AuthenticatedController {

  @Inject private PerfAdvisorService perfAdvisorService;

  @ApiOperation(
      notes = "YbaApi Internal.",
      value = "Get PA Collector",
      response = PACollectorExt.class)
  @YbaApi(visibility = YbaApi.YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.29.0.0")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result getPACollector(UUID customerUUID, UUID collectorUUID) {
    Customer.getOrBadRequest(customerUUID);
    PACollector collector = perfAdvisorService.getOrBadRequest(customerUUID, collectorUUID);
    return PlatformResults.withData(enrich(collector));
  }

  @ApiOperation(
      notes = "YbaApi Internal.",
      value = "List All PA Collectors",
      response = PACollectorExt.class,
      responseContainer = "List",
      nickname = "listAllPACollectors")
  @YbaApi(visibility = YbaApi.YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.29.0.0")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result listPACollectors(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);
    PACollectorFilter filter = PACollectorFilter.builder().customerUuid(customerUUID).build();
    List<PACollector> collectors = perfAdvisorService.list(filter);
    List<PACollectorExt> collectorExtList = collectors.stream().map(this::enrich).toList();
    return PlatformResults.withData(collectorExtList);
  }

  @ApiOperation(
      notes = "YbaApi Internal.",
      value = "Create PA Collector",
      response = PACollector.class,
      nickname = "createPACollector")
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "collectorData",
          dataType = "com.yugabyte.yw.models.PACollector",
          required = true,
          paramType = "body"))
  @YbaApi(visibility = YbaApi.YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.29.0.0")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.CREATE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result createPACollector(UUID customerUUID, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);
    PACollector collector = parseJson(request, PACollector.class);
    if (collector.getUuid() != null) {
      throw new PlatformServiceException(BAD_REQUEST, "Can't create collector with uuid set");
    }
    collector.setCustomerUUID(customerUUID);
    collector = perfAdvisorService.save(collector, false);
    auditService()
        .createAuditEntryWithReqBody(
            request,
            TargetType.PACollector,
            customerUUID.toString(),
            ActionType.CreatePACollectorConfig);
    return PlatformResults.withData(collector);
  }

  @ApiOperation(
      notes = "YbaApi Internal.",
      value = "Edit PA Collector",
      response = PACollector.class,
      nickname = "editPACollector")
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "collectorData",
          dataType = "com.yugabyte.yw.models.PACollector",
          required = true,
          paramType = "body"))
  @YbaApi(visibility = YbaApi.YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.23.0.0")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result editPACollector(
      UUID customerUUID, UUID paUUID, boolean force, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);
    PACollector collector = parseJson(request, PACollector.class);
    if (collector.getUuid() == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Can't edit collector without uuid");
    }
    if (!collector.getUuid().equals(paUUID)) {
      throw new PlatformServiceException(BAD_REQUEST, "Platform uuids do not match");
    }
    collector.setCustomerUUID(customerUUID);
    PACollector currentPlatform = perfAdvisorService.getOrBadRequest(customerUUID, paUUID);
    collector = CommonUtils.unmaskObject(currentPlatform, collector);
    collector = perfAdvisorService.save(collector, force);
    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.PACollector,
            customerUUID.toString(),
            ActionType.EditPACollectorConfig);
    return PlatformResults.withData(collector);
  }

  @ApiOperation(notes = "YbaApi Internal.", value = "Delete PA Collector", response = Boolean.class)
  @YbaApi(visibility = YbaApi.YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.23.0.0")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.DELETE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result deletePACollector(
      UUID customerUUID, UUID collectorUUID, boolean force, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);

    perfAdvisorService.delete(customerUUID, collectorUUID, force);
    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.PACollector,
            collectorUUID.toString(),
            ActionType.DeletePACollectorConfig);
    return YBPSuccess.empty();
  }

  @ApiOperation(
      notes = "YbaApi Internal.",
      value = "Check if universe is registered with PA Collector",
      response = YBPSuccess.class)
  @YbaApi(visibility = YbaApi.YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.23.0.0")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.READ),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  public Result checkRegistered(UUID customerUUID, UUID universeUUID) {
    Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID);

    UUID paCollectorUuid = universe.getUniverseDetails().getPaCollectorUuid();
    if (paCollectorUuid == null) {
      throw new PlatformServiceException(NOT_FOUND, "Universe is not registered with PA Collector");
    }

    PACollector collector = perfAdvisorService.getOrBadRequest(customerUUID, paCollectorUuid);

    boolean isRegistered = perfAdvisorService.isRegistered(collector, universe);
    if (isRegistered) {
      return YBPSuccess.empty();
    } else {
      throw new PlatformServiceException(NOT_FOUND, "Universe is not registered with PA Collector");
    }
  }

  @ApiOperation(
      notes = "YbaApi Internal.",
      value = "Register universe with PA Collector",
      response = YBPSuccess.class)
  @YbaApi(visibility = YbaApi.YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.23.0.0")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  public Result registerUniverse(
      UUID customerUUID, UUID universeUUID, UUID collectorUUID, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);

    PACollector collector = perfAdvisorService.getOrBadRequest(customerUUID, collectorUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID);

    perfAdvisorService.putUniverse(collector, universe);
    Universe.saveDetails(
        universeUUID, u -> u.getUniverseDetails().setPaCollectorUuid(collectorUUID));
    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.Universe,
            universeUUID.toString(),
            Audit.ActionType.PACollectorRegister);
    return YBPSuccess.empty();
  }

  @ApiOperation(
      notes = "YbaApi Internal.",
      value = "Unregister universe from PA Collector",
      response = YBPSuccess.class)
  @YbaApi(visibility = YbaApi.YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.23.0.0")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  public Result unregisterUniverse(UUID customerUUID, UUID universeUUID, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID);

    UUID paCollectorUuid = universe.getUniverseDetails().getPaCollectorUuid();
    if (paCollectorUuid == null) {
      return YBPSuccess.empty();
    }

    PACollector collector = perfAdvisorService.getOrBadRequest(customerUUID, paCollectorUuid);

    perfAdvisorService.deleteUniverse(collector, universe);
    Universe.saveDetails(universeUUID, u -> u.getUniverseDetails().setPaCollectorUuid(null));
    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.Universe,
            universeUUID.toString(),
            Audit.ActionType.PACollectorUnregister);
    return YBPSuccess.empty();
  }

  private PACollectorExt enrich(PACollector collector) {
    PACollectorExt collectorExt = new PACollectorExt();
    collectorExt.setPaCollector(CommonUtils.maskObject(collector));
    collectorExt.setInUseStatus(perfAdvisorService.getInUseStatus(collector));
    return collectorExt;
  }
}
