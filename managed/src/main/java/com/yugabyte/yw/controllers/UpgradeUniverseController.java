// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.inject.Inject;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.operator.annotations.BlockOperatorResource;
import com.yugabyte.yw.common.operator.annotations.OperatorResourceTypes;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.controllers.handlers.GFlagsAuditHandler;
import com.yugabyte.yw.controllers.handlers.UpgradeUniverseHandler;
import com.yugabyte.yw.forms.AuditLogConfigParams;
import com.yugabyte.yw.forms.CertsRotateParams;
import com.yugabyte.yw.forms.FinalizeUpgradeParams;
import com.yugabyte.yw.forms.GFlagsUpgradeParams;
import com.yugabyte.yw.forms.KubernetesGFlagsUpgradeParams;
import com.yugabyte.yw.forms.KubernetesOverridesUpgradeParams;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPTask;
import com.yugabyte.yw.forms.ResizeNodeParams;
import com.yugabyte.yw.forms.RestartTaskParams;
import com.yugabyte.yw.forms.RollbackUpgradeParams;
import com.yugabyte.yw.forms.RuntimeConfigFormData.ScopedConfig.ScopeType;
import com.yugabyte.yw.forms.SoftwareUpgradeParams;
import com.yugabyte.yw.forms.SystemdUpgradeParams;
import com.yugabyte.yw.forms.ThirdpartySoftwareUpgradeParams;
import com.yugabyte.yw.forms.TlsToggleParams;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.forms.VMImageUpgradeParams;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.common.YbaApi.YbaApiVisibility;
import com.yugabyte.yw.models.extended.FinalizeUpgradeInfoResponse;
import com.yugabyte.yw.models.extended.SoftwareUpgradeInfoRequest;
import com.yugabyte.yw.models.extended.SoftwareUpgradeInfoResponse;
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
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import play.mvc.Http;
import play.mvc.Http.Request;
import play.mvc.Result;

@Slf4j
@Api(
    value = "Universe Upgrades Management",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class UpgradeUniverseController extends AuthenticatedController {

  @Inject UpgradeUniverseHandler upgradeUniverseHandler;

  @Inject RuntimeConfigFactory runtimeConfigFactory;

  @Inject RuntimeConfGetter confGetter;

  @Inject GFlagsAuditHandler gFlagsAuditHandler;

  /**
   * API that restarts all nodes in the universe. Supports rolling and non-rolling restart
   *
   * @param customerUuid ID of customer
   * @param universeUuid ID of universe
   * @return Result of update operation with task id
   */
  @ApiOperation(
      value = "Restart Universe",
      notes = "Queues a task to perform a rolling restart in a universe.",
      nickname = "restartUniverse",
      response = YBPTask.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "restart_task_params",
          value = "Restart Task Params",
          dataType = "com.yugabyte.yw.forms.RestartTaskParams",
          required = true,
          paramType = "body"))
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  @BlockOperatorResource(resource = OperatorResourceTypes.UNIVERSE)
  public Result restartUniverse(UUID customerUuid, UUID universeUuid, Http.Request request) {
    return requestHandler(
        request,
        upgradeUniverseHandler::restartUniverse,
        RestartTaskParams.class,
        Audit.ActionType.Restart,
        customerUuid,
        universeUuid);
  }

  /**
   * API that upgrades YugabyteDB software version in all nodes. Supports rolling and non-rolling
   * upgrade of the universe.
   *
   * @param customerUuid ID of customer
   * @param universeUuid ID of universe
   * @return Result of update operation with task id
   */
  @ApiOperation(
      value = "Upgrade Software",
      notes = "Queues a task to perform software upgrade and rolling restart in a universe.",
      nickname = "upgradeSoftware",
      response = YBPTask.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "software_upgrade_params",
          value = "Software Upgrade Params",
          dataType = "com.yugabyte.yw.forms.SoftwareUpgradeParams",
          required = true,
          paramType = "body"))
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  @BlockOperatorResource(resource = OperatorResourceTypes.UNIVERSE)
  public Result upgradeSoftware(UUID customerUuid, UUID universeUuid, Http.Request request) {
    return requestHandler(
        request,
        upgradeUniverseHandler::upgradeSoftware,
        SoftwareUpgradeParams.class,
        Audit.ActionType.UpgradeSoftware,
        customerUuid,
        universeUuid);
  }

  /**
   * API that upgrades YugabyteDB DB version in all nodes. Supports rolling and non-rolling upgrade
   * of the universe. It also support rollback if upgrade is not finalize.
   *
   * @param customerUuid ID of customer
   * @param universeUuid ID of universe
   * @return Result of update operation with task id
   */
  @YbaApi(
      visibility = YbaApiVisibility.PREVIEW,
      sinceYBAVersion = "2.20.2.0",
      runtimeConfigScope = ScopeType.UNIVERSE)
  @ApiOperation(
      notes =
          "WARNING: This is a preview API that could change. This is a two step DB software version"
              + " upgrade, Upgrade DB version and then finalize software which would be same as of"
              + " upgrade software but additionally support rollback before upgrade finalize. ",
      value = "Upgrade DB version",
      nickname = "upgradeDBVersion",
      response = YBPTask.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "software_upgrade_params",
          value = "Software Upgrade Params",
          dataType = "com.yugabyte.yw.forms.SoftwareUpgradeParams",
          required = true,
          paramType = "body"))
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  @BlockOperatorResource(resource = OperatorResourceTypes.UNIVERSE)
  public Result upgradeDBVersion(UUID customerUuid, UUID universeUuid, Http.Request request) {
    return requestHandler(
        request,
        upgradeUniverseHandler::upgradeDBVersion,
        SoftwareUpgradeParams.class,
        Audit.ActionType.UpgradeSoftware,
        customerUuid,
        universeUuid);
  }

  /**
   * API that finalize YugabyteDB software version upgrade on a universe.
   *
   * @param customerUuid ID of customer
   * @param universeUuid ID of universe
   * @return Result of update operation with task id
   */
  @YbaApi(
      visibility = YbaApiVisibility.PREVIEW,
      sinceYBAVersion = "2.20.2.0",
      runtimeConfigScope = ScopeType.UNIVERSE)
  @ApiOperation(
      notes =
          "WARNING: This is a preview API that could change. Queues a task to finalize upgrade in a"
              + " universe.",
      value = "Finalize Upgrade",
      nickname = "finalizeUpgrade",
      response = YBPTask.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "finalize_upgrade_params",
          value = "Finalize Upgrade Params",
          dataType = "com.yugabyte.yw.forms.FinalizeUpgradeParams",
          required = true,
          paramType = "body"))
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  public Result finalizeUpgrade(UUID customerUuid, UUID universeUuid, Http.Request request) {
    return requestHandler(
        request,
        upgradeUniverseHandler::finalizeUpgrade,
        FinalizeUpgradeParams.class,
        Audit.ActionType.FinalizeUpgrade,
        customerUuid,
        universeUuid);
  }

  /**
   * API that Rollback YugabyteDB software version upgrade on a universe.
   *
   * @param customerUuid ID of customer
   * @param universeUuid ID of universe
   * @return Result of update operation with task id
   */
  @YbaApi(
      visibility = YbaApiVisibility.PREVIEW,
      sinceYBAVersion = "2.20.2.0",
      runtimeConfigScope = ScopeType.UNIVERSE)
  @ApiOperation(
      notes =
          "WARNING: This is a preview API that could change. Queues a task to rollback upgrade in a"
              + " universe.",
      value = "Rollback Upgrade",
      nickname = "rollbackUpgrade",
      response = YBPTask.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "rollback_upgrade_params",
          value = "RollBack Upgrade Params",
          dataType = "com.yugabyte.yw.forms.RollbackUpgradeParams",
          required = true,
          paramType = "body"))
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  public Result rollbackUpgrade(UUID customerUuid, UUID universeUuid, Http.Request request) {
    return requestHandler(
        request,
        upgradeUniverseHandler::rollbackUpgrade,
        RollbackUpgradeParams.class,
        Audit.ActionType.RollbackUpgrade,
        customerUuid,
        universeUuid);
  }

  /**
   * API that upgrades gflags in all nodes of primary cluster. Supports rolling, non-rolling, and
   * non-restart upgrades upgrade of the universe.
   *
   * @param customerUuid ID of customer
   * @param universeUuid ID of universe
   * @return Result of update operation with task id
   */
  @ApiOperation(
      value = "Upgrade GFlags",
      notes = "Queues a task to perform gflags upgrade and rolling restart in a universe.",
      nickname = "upgradeGFlags",
      response = YBPTask.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "gflags_upgrade_params",
          value = "GFlags Upgrade Params",
          dataType = "com.yugabyte.yw.forms.GFlagsUpgradeParams",
          required = true,
          paramType = "body"))
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  @BlockOperatorResource(resource = OperatorResourceTypes.UNIVERSE)
  public Result upgradeGFlags(UUID customerUuid, UUID universeUuid, Http.Request request) {
    Customer customer = Customer.getOrBadRequest(customerUuid);
    Universe universe = Universe.getOrBadRequest(universeUuid, customer);
    Class<? extends GFlagsUpgradeParams> flagParamType;
    if (Util.isKubernetesBasedUniverse(universe)) {
      flagParamType = KubernetesGFlagsUpgradeParams.class;
    } else {
      flagParamType = GFlagsUpgradeParams.class;
    }

    return requestHandler(
        request,
        upgradeUniverseHandler::upgradeGFlags,
        flagParamType,
        Audit.ActionType.UpgradeGFlags,
        customerUuid,
        universeUuid);
  }

  /**
   * API that upgrades kubernetes overrides for primary and read clusters.
   *
   * @param customerUuid ID of customer
   * @param universeUuid ID of universe
   * @return Result of update operation with task id
   */
  @ApiOperation(
      value = "Upgrade KubernetesOverrides",
      notes = "Queues a task to perform Kubernetesoverrides upgrade for a kubernetes universe.",
      nickname = "upgradeKubernetesOverrides",
      response = YBPTask.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "Kubernetes_overrides_upgrade_params",
          value = "Kubernetes Override Upgrade Params",
          dataType = "com.yugabyte.yw.forms.KubernetesOverridesUpgradeParams",
          required = true,
          paramType = "body"))
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  @BlockOperatorResource(resource = OperatorResourceTypes.UNIVERSE)
  public Result upgradeKubernetesOverrides(
      UUID customerUuid, UUID universeUuid, Http.Request request) {
    return requestHandler(
        request,
        upgradeUniverseHandler::upgradeKubernetesOverrides,
        KubernetesOverridesUpgradeParams.class,
        Audit.ActionType.UpgradeKubernetesOverrides,
        customerUuid,
        universeUuid);
  }

  /**
   * API that rotates custom certificates for onprem universes. Supports rolling and non-rolling
   * upgrade of the universe.
   *
   * @param customerUuid ID of customer
   * @param universeUuid ID of universe
   * @return Result of update operation with task id
   */
  @ApiOperation(
      value = "Upgrade Certs",
      notes = "Queues a task to perform certificate rotation and rolling restart in a universe.",
      nickname = "upgradeCerts",
      response = YBPTask.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "certs_rotate_params",
          value = "Certs Rotate Params",
          dataType = "com.yugabyte.yw.forms.CertsRotateParams",
          required = true,
          paramType = "body"))
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  @BlockOperatorResource(resource = OperatorResourceTypes.UNIVERSE)
  public Result upgradeCerts(UUID customerUuid, UUID universeUuid, Http.Request request) {
    return requestHandler(
        request,
        upgradeUniverseHandler::rotateCerts,
        CertsRotateParams.class,
        Audit.ActionType.UpgradeCerts,
        customerUuid,
        universeUuid);
  }

  /**
   * API that toggles TLS state of the universe. Can enable/disable node to node and client to node
   * encryption. Supports rolling and non-rolling upgrade of the universe.
   *
   * @param customerUuid ID of customer
   * @param universeUuid ID of universe
   * @return Result of update operation with task id
   */
  @ApiOperation(
      value = "Upgrade TLS",
      notes = "Queues a task to perform TLS ugprade and rolling restart in a universe.",
      nickname = "upgradeTls",
      response = YBPTask.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "tls_toggle_params",
          value = "TLS Toggle Params",
          dataType = "com.yugabyte.yw.forms.TlsToggleParams",
          required = true,
          paramType = "body"))
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  @BlockOperatorResource(resource = OperatorResourceTypes.UNIVERSE)
  public Result upgradeTls(UUID customerUuid, UUID universeUuid, Http.Request request) {
    return requestHandler(
        request,
        upgradeUniverseHandler::toggleTls,
        TlsToggleParams.class,
        Audit.ActionType.ToggleTls,
        customerUuid,
        universeUuid);
  }

  /**
   * API to modify the audit logging configuration for a universe.
   *
   * @param customerUuid ID of the customer
   * @param universeUuid ID of the universe
   * @param request HTTP request object
   * @return Result indicating the success of the modification operation
   */
  @ApiOperation(
      notes = "YbaApi Internal. Modifies the audit logging configuration for a universe.",
      value = "Modify Audit Logging Configuration",
      nickname = "modifyAuditLogging",
      response = YBPTask.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "auditLoggingConfig",
          value = "Audit Logging Configuration",
          dataType = "com.yugabyte.yw.forms.AuditLogConfigParams",
          required = true,
          paramType = "body"))
  @YbaApi(visibility = YbaApi.YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.20.0.0")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  public Result modifyAuditLogging(UUID customerUuid, UUID universeUuid, Http.Request request) {
    return requestHandler(
        request,
        upgradeUniverseHandler::modifyAuditLoggingConfig,
        AuditLogConfigParams.class,
        Audit.ActionType.ModifyAuditLogging,
        customerUuid,
        universeUuid);
  }

  /**
   * API that resizes nodes in the universe. Supports only rolling upgrade.
   *
   * @param customerUuid ID of customer
   * @param universeUuid ID of universe
   * @return Result of update operation with task id
   */
  @ApiOperation(
      value = "Resize Node",
      notes = "Queues a task to perform node resize and rolling restart in a universe.",
      nickname = "resizeNode",
      response = YBPTask.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "resize_node_params",
          value = "Resize Node Params",
          dataType = "com.yugabyte.yw.forms.ResizeNodeParams",
          required = true,
          paramType = "body"))
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  @BlockOperatorResource(resource = OperatorResourceTypes.UNIVERSE)
  public Result resizeNode(UUID customerUuid, UUID universeUuid, Http.Request request) {
    return requestHandler(
        request,
        upgradeUniverseHandler::resizeNode,
        ResizeNodeParams.class,
        Audit.ActionType.ResizeNode,
        customerUuid,
        universeUuid);
  }

  /**
   * API that upgrades third-party software on nodes in the universe. Supports only rolling upgrade.
   *
   * @param customerUuid ID of customer
   * @param universeUuid ID of universe
   * @return Result of update operation with task id
   */
  @ApiOperation(
      value = "Upgrade third-party software",
      notes = "Queues a task to perform upgrade third-party software in a universe.",
      nickname = "upgradeThirdpartySoftware",
      response = YBPTask.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "thirdparty_software_upgrade_params",
          value = "Thirdparty Software Upgrade Params",
          dataType = "com.yugabyte.yw.forms.ThirdpartySoftwareUpgradeParams",
          required = true,
          paramType = "body"))
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  public Result upgradeThirdpartySoftware(
      UUID customerUuid, UUID universeUuid, Http.Request request) {
    return requestHandler(
        request,
        upgradeUniverseHandler::thirdpartySoftwareUpgrade,
        ThirdpartySoftwareUpgradeParams.class,
        Audit.ActionType.ThirdpartySoftwareUpgrade,
        customerUuid,
        universeUuid);
  }

  /**
   * API that upgrades VM Image for AWS and GCP based universes. Supports only rolling upgrade of
   * the universe.
   *
   * @param customerUuid ID of customer
   * @param universeUuid ID of universe
   * @return Result of update operation with task id
   */
  @ApiOperation(
      value = "Upgrade VM Image",
      notes = "Queues a task to perform VM Image upgrade and rolling restart in a universe.",
      nickname = "upgradeVMImage",
      response = YBPTask.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "vmimage_upgrade_params",
          value = "VM Image Upgrade Params",
          dataType = "com.yugabyte.yw.forms.VMImageUpgradeParams",
          required = true,
          paramType = "body"))
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  @BlockOperatorResource(resource = OperatorResourceTypes.UNIVERSE)
  public Result upgradeVMImage(UUID customerUuid, UUID universeUuid, Http.Request request) {
    Customer customer = Customer.getOrBadRequest(customerUuid);
    Universe universe = Universe.getOrBadRequest(universeUuid, customer);

    // TODO yb.cloud.enabled is redundant here because many tests set it during runtime,
    // to enable this method in cloud. Clean it up later when the tests are fixed.
    if (!runtimeConfigFactory.forCustomer(customer).getBoolean("yb.cloud.enabled")
        && !confGetter.getConfForScope(universe, UniverseConfKeys.ybUpgradeVmImage)) {
      throw new PlatformServiceException(METHOD_NOT_ALLOWED, "VM image upgrade is disabled.");
    }

    return requestHandler(
        request,
        upgradeUniverseHandler::upgradeVMImage,
        VMImageUpgradeParams.class,
        Audit.ActionType.UpgradeVmImage,
        customerUuid,
        universeUuid);
  }

  /**
   * API that upgrades from cron to systemd for universes. Supports only rolling upgrade of the
   * universe.
   *
   * @param customerUUID ID of customer
   * @param universeUUID ID of universe
   * @return Result of update operation with task id
   */
  @ApiOperation(
      value = "Upgrade Systemd",
      notes = "Queues a task to perform systemd upgrade and rolling restart in a universe.",
      nickname = "upgradeSystemd",
      response = YBPTask.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "systemd_upgrade_params",
          value = "Systemd Upgrade Params",
          dataType = "com.yugabyte.yw.forms.SystemdUpgradeParams",
          required = true,
          paramType = "body"))
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  @BlockOperatorResource(resource = OperatorResourceTypes.UNIVERSE)
  public Result upgradeSystemd(UUID customerUUID, UUID universeUUID, Http.Request request) {
    return requestHandler(
        request,
        upgradeUniverseHandler::upgradeSystemd,
        SystemdUpgradeParams.class,
        Audit.ActionType.UpgradeSystemd,
        customerUUID,
        universeUUID);
  }

  /**
   * API that reboots all nodes in the universe. Only supports rolling upgrade.
   *
   * @param customerUUID ID of customer
   * @param universeUUID ID of universe
   * @return Result of update operation with task id
   */
  @ApiOperation(
      value = "Reboot universe",
      notes = "Queues a task to perform a rolling reboot in a universe.",
      nickname = "rebootUniverse",
      response = YBPTask.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "upgrade_task_params",
          value = "Upgrade Task Params",
          dataType = "com.yugabyte.yw.forms.UpgradeTaskParams",
          required = true,
          paramType = "body"))
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  @BlockOperatorResource(resource = OperatorResourceTypes.UNIVERSE)
  public Result rebootUniverse(UUID customerUUID, UUID universeUUID, Http.Request request) {
    return requestHandler(
        request,
        upgradeUniverseHandler::rebootUniverse,
        UpgradeTaskParams.class,
        Audit.ActionType.RebootUniverse,
        customerUUID,
        universeUUID);
  }

  /**
   * API that performs pre-check and provides pre-upgrade info in the universe.
   *
   * @param customerUUID ID of customer
   * @param universeUUID ID of universe
   * @return Pre upgrade info
   */
  @YbaApi(
      visibility = YbaApiVisibility.PREVIEW,
      sinceYBAVersion = "2.20.2.0",
      runtimeConfigScope = ScopeType.UNIVERSE)
  @ApiOperation(
      notes =
          "WARNING: This is a preview API that could change. Performs pre-checks and provides"
              + " pre-upgrade info.",
      value = "Software Upgrade universe pre-check",
      nickname = "softwareUpgradePreCheck",
      response = SoftwareUpgradeInfoResponse.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "software_upgrade_info_request",
          value = "Software Upgrade Info Request",
          dataType = "com.yugabyte.yw.models.extended.SoftwareUpgradeInfoRequest",
          required = true,
          paramType = "body"))
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  public Result softwareUpgradePreCheck(
      UUID customerUUID, UUID universeUUID, Http.Request request) {
    SoftwareUpgradeInfoRequest infoRequest =
        parseJsonAndValidate(request, SoftwareUpgradeInfoRequest.class);
    SoftwareUpgradeInfoResponse result =
        upgradeUniverseHandler.softwareUpgradeInfo(customerUUID, universeUUID, infoRequest);
    return PlatformResults.withData(result);
  }

  /**
   * API that provides pre-finalize upgrade info in the universe.
   *
   * @param customerUUID ID of customer
   * @param universeUUID ID of universe
   * @return Pre Finalize upgrade info
   */
  @YbaApi(
      visibility = YbaApiVisibility.PREVIEW,
      sinceYBAVersion = "2.20.2.0",
      runtimeConfigScope = ScopeType.UNIVERSE)
  @ApiOperation(
      notes =
          "WARNING: This is a preview API that could change. Provides pre-finalize software upgrade"
              + " info.",
      value = "Finalize Software Upgrade info",
      nickname = "preFinalizeSoftwareUpgradeInfo",
      response = FinalizeUpgradeInfoResponse.class)
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  public Result finalizeUpgradeInfo(UUID customerUUID, UUID universeUUID) {
    FinalizeUpgradeInfoResponse response =
        upgradeUniverseHandler.finalizeUpgradeInfo(customerUUID, universeUUID);
    return PlatformResults.withData(response);
  }

  private <T extends UpgradeTaskParams> Result requestHandler(
      Request request,
      IUpgradeUniverseHandlerMethod<T> serviceMethod,
      Class<T> type,
      Audit.ActionType auditActionType,
      UUID customerUuid,
      UUID universeUuid) {
    Customer customer = Customer.getOrBadRequest(customerUuid);
    Universe universe = Universe.getOrBadRequest(universeUuid, customer);
    T requestParams =
        UniverseControllerRequestBinder.bindFormDataToUpgradeTaskParams(request, type, universe);

    log.info(
        "Upgrade for universe {} [ {} ] customer {}.",
        universe.getName(),
        universe.getUniverseUUID(),
        customer.getUuid());
    JsonNode additionalDetails = null;
    if (GFlagsUpgradeParams.class.isAssignableFrom(type)) {
      log.debug("setting up gflag audit logging");
      additionalDetails =
          gFlagsAuditHandler.constructGFlagAuditPayload((GFlagsUpgradeParams) requestParams);
    }
    UUID taskUuid = serviceMethod.upgrade(requestParams, customer, universe);
    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.Universe,
            universeUuid.toString(),
            auditActionType,
            request.body().asJson(),
            taskUuid,
            additionalDetails);
    return new YBPTask(taskUuid, universe.getUniverseUUID()).asResult();
  }
}
