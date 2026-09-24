// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.controllers;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.tasks.RegisterUniverseWithPACollector;
import com.yugabyte.yw.commissioner.tasks.UnregisterUniverseFromPACollector;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.pa.PaRegistrationMode;
import com.yugabyte.yw.common.pa.PerfAdvisorEndpointService;
import com.yugabyte.yw.common.pa.PerfAdvisorService;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.forms.PACollectorExt;
import com.yugabyte.yw.forms.PaRegistrationStatusResponse;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.forms.PlatformResults.YBPTask;
import com.yugabyte.yw.forms.paging.PaUniversePagedApiQuery;
import com.yugabyte.yw.forms.paging.PaUniversePagedApiResponse;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Audit.ActionType;
import com.yugabyte.yw.models.Audit.TargetType;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.PACollector;
import com.yugabyte.yw.models.PerfAdvisorEndpoint;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.filters.PACollectorFilter;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.TaskType;
import com.yugabyte.yw.rbac.annotations.AuthzPath;
import com.yugabyte.yw.rbac.annotations.PermissionAttribute;
import com.yugabyte.yw.rbac.annotations.RequiredPermissionOnResource;
import com.yugabyte.yw.rbac.annotations.Resource;
import com.yugabyte.yw.rbac.enums.SourceType;
import io.swagger.annotations.*;
import java.util.Arrays;
import java.util.List;
import java.util.Optional;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.lang3.StringUtils;
import play.mvc.Http;
import play.mvc.Result;

@Api(
    value = "PA Collector",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
@Slf4j
public class PACollectorController extends AuthenticatedController {

  @Inject private PerfAdvisorService perfAdvisorService;
  @Inject private PerfAdvisorEndpointService perfAdvisorEndpointService;
  @Inject private Commissioner commissioner;

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
    if (collector.isEmbedded()) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Can't create embedded collector via API - it is managed by YBA");
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
    if (currentPlatform.isEmbedded()) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Can't edit embedded collector via API - it is managed by YBA");
    }
    if (collector.isEmbedded()) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Can't set embedded flag via API - it is managed by YBA");
    }
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
    PACollector collector = perfAdvisorService.getOrBadRequest(customerUUID, collectorUUID);
    if (collector.isEmbedded()) {
      // The embedded collector is fully owned by PACollectorSync - deleting it
      // via the API would just leave it half-configured until the next initializer tick
      // recreates it. Force users to disable it via runtime config (yb.pa.url) instead.
      throw new PlatformServiceException(
          BAD_REQUEST, "Can't delete embedded collector via API - it is managed by YBA");
    }

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
      response = PaRegistrationStatusResponse.class)
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

    var metadata = perfAdvisorService.getUniverseMetadata(collector, universe);
    if (metadata == null) {
      throw new PlatformServiceException(NOT_FOUND, "Universe is not registered with PA Collector");
    }

    PaRegistrationMode mode = PaRegistrationMode.of(metadata);
    // The collector reports its own export config ids, which are Perf Advisor Endpoint uuids by
    // construction. Resolved to a local record so callers see a name rather than a bare uuid.
    UUID endpointUuid =
        CollectionUtils.isEmpty(metadata.getExportConfigIds())
            ? null
            : metadata.getExportConfigIds().get(0);
    String endpointName =
        endpointUuid == null
            ? null
            : Optional.ofNullable(perfAdvisorEndpointService.get(customerUUID, endpointUuid))
                .map(PerfAdvisorEndpoint::getName)
                .orElse(null);
    return PlatformResults.withData(
        new PaRegistrationStatusResponse(
            true, metadata.isMetricsExportToPrometheusEnabled(), mode, endpointUuid, endpointName));
  }

  @ApiOperation(
      notes = "YbaApi Internal.",
      value = "Register universe with PA Collector",
      response = YBPTask.class)
  @YbaApi(visibility = YbaApi.YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.23.0.0")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  public Result registerUniverse(
      UUID customerUUID,
      UUID universeUUID,
      UUID collectorUUID,
      Boolean advancedObservability,
      String mode,
      UUID paEndpointUUID,
      Http.Request request) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    PACollector collector = perfAdvisorService.getOrBadRequest(customerUUID, collectorUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID);

    PaRegistrationMode targetRegistrationMode =
        resolveRegistrationMode(mode, advancedObservability, paEndpointUUID);
    if (targetRegistrationMode.requiresExportConfig()) {
      // Gated here rather than inside the task: online mode is off by default, and a task that can
      // only fail is a worse answer than a rejected request. Resolving the endpoint now also turns
      // an unknown uuid into a bad request instead of a failed subtask.
      perfAdvisorEndpointService.checkEnabled(customerUUID);
      perfAdvisorEndpointService.getOrBadRequest(customerUUID, paEndpointUUID);
    }

    PerfAdvisorService.PaMemoryMode currentMode = currentPaMemoryMode(universe, collector);
    PerfAdvisorService.PaMemoryMode targetMode =
        PerfAdvisorService.toMemoryMode(targetRegistrationMode);
    perfAdvisorService.validatePerfAdvisorMemory(
        universe,
        currentMode,
        targetMode,
        "Cannot register universe with Performance Advisor Collector");

    RegisterUniverseWithPACollector.Params params = new RegisterUniverseWithPACollector.Params();
    params.setUniverseUUID(universeUUID);
    params.customerUuid = customerUUID;
    params.paCollectorUuid = collectorUUID;
    params.mode = targetRegistrationMode;
    params.paEndpointUuid = paEndpointUUID;

    UUID taskUUID = commissioner.submit(TaskType.RegisterUniverseWithPACollector, params);
    CustomerTask.create(
        customer,
        universeUUID,
        taskUUID,
        CustomerTask.TargetType.Universe,
        CustomerTask.TaskType.RegisterWithPACollector,
        universe.getName(),
        registrationTaskDescription(universe, targetRegistrationMode));
    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.Universe,
            universeUUID.toString(),
            Audit.ActionType.PACollectorRegister);
    return new YBPTask(taskUUID, universeUUID).asResult();
  }

  @ApiOperation(
      notes = "YbaApi Internal.",
      value = "Unregister universe from PA Collector",
      response = YBPTask.class)
  @YbaApi(visibility = YbaApi.YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.23.0.0")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  public Result unregisterUniverse(UUID customerUUID, UUID universeUUID, Http.Request request) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID);

    UUID paCollectorUuid = universe.getUniverseDetails().getPaCollectorUuid();
    if (paCollectorUuid == null) {
      return YBPSuccess.empty();
    }

    perfAdvisorService.getOrBadRequest(customerUUID, paCollectorUuid);

    UnregisterUniverseFromPACollector.Params params =
        new UnregisterUniverseFromPACollector.Params();
    params.setUniverseUUID(universeUUID);
    params.customerUuid = customerUUID;
    params.paCollectorUuid = paCollectorUuid;

    UUID taskUUID = commissioner.submit(TaskType.UnregisterUniverseFromPACollector, params);
    CustomerTask.create(
        customer,
        universeUUID,
        taskUUID,
        CustomerTask.TargetType.Universe,
        CustomerTask.TaskType.UnregisterFromPACollector,
        universe.getName(),
        "Disable PA Collector For");
    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.Universe,
            universeUUID.toString(),
            Audit.ActionType.PACollectorUnregister);
    return new YBPTask(taskUUID, universeUUID).asResult();
  }

  @ApiOperation(
      notes = "YbaApi Internal.",
      value = "List universes registered with PA Collector (paginated)",
      response = PaUniversePagedApiResponse.class,
      nickname = "pageRegisteredUniverses")
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "PagePaUniverseRequest",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.paging.PaUniversePagedApiQuery",
          required = true))
  @YbaApi(visibility = YbaApi.YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.29.0.0")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result pageRegisteredUniverses(
      UUID customerUUID, UUID collectorUUID, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);
    PACollector collector = perfAdvisorService.getOrBadRequest(customerUUID, collectorUUID);
    PaUniversePagedApiQuery apiQuery = parseJsonAndValidate(request, PaUniversePagedApiQuery.class);
    PaUniversePagedApiResponse response =
        perfAdvisorService.pagedListRegisteredUniverses(collector, apiQuery);
    return PlatformResults.withData(response);
  }

  private PACollectorExt enrich(PACollector collector) {
    PACollectorExt collectorExt = new PACollectorExt();
    collectorExt.setPaCollector(CommonUtils.maskObject(collector));
    collectorExt.setInUseStatus(perfAdvisorService.getInUseStatus(collector));
    return collectorExt;
  }

  private PerfAdvisorService.PaMemoryMode currentPaMemoryMode(
      Universe universe, PACollector collector) {
    if (universe.getUniverseDetails().getPaCollectorUuid() == null) {
      return PerfAdvisorService.PaMemoryMode.NONE;
    }
    var metadata = perfAdvisorService.getUniverseMetadata(collector, universe);
    if (metadata == null) {
      // Universe is marked as registered locally but the collector has no record.
      // Be conservative and treat as not currently consuming any PA memory.
      return PerfAdvisorService.PaMemoryMode.NONE;
    }
    return PerfAdvisorService.toMemoryMode(PaRegistrationMode.of(metadata));
  }

  /**
   * The mode query parameter supersedes {@code advancedObservability}, which predates online mode
   * and stays accepted so existing callers keep working.
   */
  private PaRegistrationMode resolveRegistrationMode(
      String mode, Boolean advancedObservability, UUID paEndpointUUID) {
    PaRegistrationMode registrationMode;
    if (StringUtils.isEmpty(mode)) {
      registrationMode = PaRegistrationMode.of(Boolean.TRUE.equals(advancedObservability));
    } else {
      try {
        registrationMode = PaRegistrationMode.valueOf(mode.toUpperCase());
      } catch (IllegalArgumentException e) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            "Unknown registration mode '"
                + mode
                + "'. Expected one of "
                + Arrays.toString(PaRegistrationMode.values()));
      }
    }
    // PA rejects an ONLINE universe with no destination too, but failing here keeps the user out
    // of a task that can only fail.
    if (registrationMode.requiresExportConfig() && paEndpointUUID == null) {
      throw new PlatformServiceException(
          BAD_REQUEST, "A Perf Advisor Endpoint is required to register a universe in ONLINE mode");
    }
    if (!registrationMode.requiresExportConfig() && paEndpointUUID != null) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "A Perf Advisor Endpoint only applies to ONLINE mode, not " + registrationMode);
    }
    return registrationMode;
  }

  private static String registrationTaskDescription(Universe universe, PaRegistrationMode mode) {
    if (universe.getUniverseDetails().getPaCollectorUuid() == null) {
      return "Enable PA Collector For";
    }
    return switch (mode) {
      case ADVANCED -> "Enable Advanced Observability For";
      case BASIC -> "Disable Advanced Observability For";
      case ONLINE -> "Enable Online Perf Advisor Collection For";
    };
  }
}
