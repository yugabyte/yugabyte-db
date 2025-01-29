// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.controllers;

import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.tasks.DeletePitrConfig;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.backuprestore.BackupUtil;
import com.yugabyte.yw.common.backuprestore.BackupUtil.ApiType;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.CloneNamespaceParams;
import com.yugabyte.yw.forms.CreatePitrConfigParams;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.forms.PlatformResults.YBPTask;
import com.yugabyte.yw.forms.RestoreSnapshotScheduleParams;
import com.yugabyte.yw.forms.UpdatePitrConfigParams;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.PitrConfig;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
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
import java.util.LinkedList;
import java.util.List;
import java.util.Optional;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.CommonTypes.TableType;
import org.yb.client.ListSnapshotSchedulesResponse;
import org.yb.client.SnapshotScheduleInfo;
import org.yb.client.YBClient;
import org.yb.master.CatalogEntityInfo.SysSnapshotEntryPB.State;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Result;

@Api(
    value = "PITR management",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
@Slf4j
public class PitrController extends AuthenticatedController {

  public static final String PITR_COMPATIBLE_DB_VERSION = "2.14.0.0-b1";
  public static final String PITR_CLONE_COMPATIBLE_PREVIEW_DB_VERSION = "2.25.1.0-b1";
  public static final String PITR_CLONE_COMPATIBLE_STABLE_DB_VERSION = "2024.2.0.0-b1";

  Commissioner commissioner;
  YBClientService ybClientService;

  @Inject
  public PitrController(Commissioner commissioner, YBClientService ybClientService) {
    this.commissioner = commissioner;
    this.ybClientService = ybClientService;
  }

  @ApiOperation(
      value = "Create pitr config for a keyspace in a universe",
      nickname = "createPitrConfig",
      response = YBPTask.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "pitrConfig",
          value = "post pitr config",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.CreatePitrConfigParams",
          required = true))
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(
                resourceType = ResourceType.UNIVERSE,
                action = Action.BACKUP_RESTORE),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  public Result createPitrConfig(
      UUID customerUUID,
      UUID universeUUID,
      String tableType,
      String keyspaceName,
      Http.Request request) {
    // Validate customer UUID
    Customer customer = Customer.getOrBadRequest(customerUUID);

    // Validate universe UUID
    Universe universe = Universe.getOrBadRequest(universeUUID, customer);
    if (universe.getUniverseDetails().universePaused) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot enable PITR when the universe is in paused state");
    } else if (universe.getUniverseDetails().updateInProgress) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot enable PITR when the universe is in locked state");
    }

    checkCompatibleYbVersion(
        universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion);
    CreatePitrConfigParams taskParams = parseJsonAndValidate(request, CreatePitrConfigParams.class);

    if (taskParams.retentionPeriodInSeconds <= 0L) {
      throw new PlatformServiceException(
          BAD_REQUEST, "PITR Config retention period cannot be less than 1 second");
    }

    if (taskParams.retentionPeriodInSeconds <= taskParams.intervalInSeconds) {
      throw new PlatformServiceException(
          BAD_REQUEST, "PITR Config interval cannot be less than retention period");
    }

    TableType type = BackupUtil.API_TYPE_TO_TABLE_TYPE_MAP.get(ApiType.valueOf(tableType));
    Optional<PitrConfig> pitrConfig = PitrConfig.maybeGet(universeUUID, type, keyspaceName);
    if (pitrConfig.isPresent()) {
      throw new PlatformServiceException(BAD_REQUEST, "PITR Config is already present");
    }

    BackupUtil.checkApiEnabled(type, universe.getUniverseDetails().getPrimaryCluster().userIntent);

    taskParams.setUniverseUUID(universeUUID);
    taskParams.customerUUID = customerUUID;
    taskParams.tableType = type;
    taskParams.keyspaceName = keyspaceName;
    UUID taskUUID = commissioner.submit(TaskType.CreatePitrConfig, taskParams);
    CustomerTask.create(
        customer,
        universeUUID,
        taskUUID,
        CustomerTask.TargetType.Universe,
        CustomerTask.TaskType.CreatePitrConfig,
        universe.getName());

    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.Universe,
            universeUUID.toString(),
            Audit.ActionType.CreatePitrConfig,
            Json.toJson(taskParams),
            taskUUID);
    return new YBPTask(taskUUID).asResult();
  }

  @ApiOperation(
      value = "Update pitr config for a keyspace in a universe",
      nickname = "updatePitrConfig",
      response = YBPTask.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "pitrConfig",
          value = "put pitr config",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.UpdatePitrConfigParams",
          required = true))
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(
                resourceType = ResourceType.UNIVERSE,
                action = Action.BACKUP_RESTORE),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  public Result updatePitrConfig(
      UUID customerUUID, UUID universeUUID, UUID pitrConfigUUID, Http.Request request) {
    // Validate customer UUID
    Customer customer = Customer.getOrBadRequest(customerUUID);
    // Validate universe UUID
    Universe universe = Universe.getOrBadRequest(universeUUID, customer);
    if (universe.getUniverseDetails().universePaused) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot update PITR when the universe is in paused state");
    } else if (universe.getUniverseDetails().updateInProgress) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot update PITR when the universe is in locked state");
    }

    PitrConfig pitrConfig = PitrConfig.getOrBadRequest(pitrConfigUUID);

    checkCompatibleYbVersion(
        universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion);
    UpdatePitrConfigParams taskParams = parseJsonAndValidate(request, UpdatePitrConfigParams.class);

    if (taskParams.retentionPeriodInSeconds <= 0L) {
      throw new PlatformServiceException(
          BAD_REQUEST, "PITR Config retention period cannot be less than 1 second");
    }

    if (taskParams.retentionPeriodInSeconds <= taskParams.intervalInSeconds) {
      throw new PlatformServiceException(
          BAD_REQUEST, "PITR Config interval cannot be less than retention period");
    }

    if (taskParams.retentionPeriodInSeconds == pitrConfig.getRetentionPeriod()
        && taskParams.intervalInSeconds == pitrConfig.getScheduleInterval()) {
      throw new PlatformServiceException(BAD_REQUEST, "Nothing to update in the PITR config");
    }

    taskParams.setUniverseUUID(universeUUID);
    taskParams.customerUUID = customerUUID;
    taskParams.pitrConfigUUID = pitrConfig.getUuid();
    UUID taskUUID = commissioner.submit(TaskType.UpdatePitrConfig, taskParams);
    CustomerTask.create(
        customer,
        universeUUID,
        taskUUID,
        CustomerTask.TargetType.Universe,
        CustomerTask.TaskType.UpdatePitrConfig,
        universe.getName());

    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.Universe,
            universeUUID.toString(),
            Audit.ActionType.UpdatePitrConfig,
            Json.toJson(taskParams),
            taskUUID);
    return new YBPTask(taskUUID).asResult();
  }

  @ApiOperation(
      value = "List the PITR configs of a universe",
      response = PitrConfig.class,
      responseContainer = "List",
      nickname = "ListOfPitrConfigs")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.READ),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  public Result listPitrConfigs(UUID customerUUID, UUID universeUUID) {
    // Validate customer UUID
    Customer customer = Customer.getOrBadRequest(customerUUID);
    // Validate universe UUID
    Universe universe = Universe.getOrBadRequest(universeUUID, customer);

    List<PitrConfig> pitrConfigList = new LinkedList<>();
    String masterHostPorts = universe.getMasterAddresses();
    String certificate = universe.getCertificateNodetoNode();
    YBClient client = null;
    ListSnapshotSchedulesResponse scheduleResp;
    List<SnapshotScheduleInfo> scheduleInfoList = null;

    checkCompatibleYbVersion(
        universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion);
    if (universe.getUniverseDetails().universePaused) {
      pitrConfigList = createPitrConfigsWithUnknownState(universeUUID);
    } else {
      try {
        client = ybClientService.getClient(masterHostPorts, certificate);
        scheduleResp = client.listSnapshotSchedules(null);
        scheduleInfoList = scheduleResp.getSnapshotScheduleInfoList();
      } catch (Exception ex) {
        log.error(ex.getMessage());
        throw new PlatformServiceException(INTERNAL_SERVER_ERROR, ex.getMessage());
      } finally {
        ybClientService.closeClient(client, masterHostPorts);
      }

      if (scheduleResp.hasError()) {
        pitrConfigList = createPitrConfigsWithUnknownState(universeUUID);
      } else {
        for (SnapshotScheduleInfo snapshotScheduleInfo : scheduleInfoList) {
          PitrConfig pitrConfig = PitrConfig.get(snapshotScheduleInfo.getSnapshotScheduleUUID());
          if (pitrConfig == null) {
            continue;
          }
          boolean pitrStatus =
              BackupUtil.allSnapshotsSuccessful(snapshotScheduleInfo.getSnapshotInfoList());
          long currentTimeMillis = System.currentTimeMillis();
          long minTimeInMillis =
              BackupUtil.getMinRecoveryTimeForSchedule(
                  snapshotScheduleInfo.getSnapshotInfoList(), pitrConfig.getRetentionPeriod());
          pitrConfig.setMinRecoverTimeInMillis(minTimeInMillis);
          pitrConfig.setMaxRecoverTimeInMillis(currentTimeMillis);
          pitrConfig.setState(pitrStatus ? State.COMPLETE : State.FAILED);
          pitrConfigList.add(pitrConfig);
        }
      }
    }
    return PlatformResults.withData(pitrConfigList);
  }

  @ApiOperation(
      value = "Perform PITR on a universe",
      nickname = "performPitr",
      response = YBPTask.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "performPitr",
          value = "perform PITR",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.RestoreSnapshotScheduleParams",
          required = true))
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(
                resourceType = ResourceType.UNIVERSE,
                action = Action.BACKUP_RESTORE),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  public Result restore(UUID customerUUID, UUID universeUUID, Http.Request request) {
    log.info("Received restore PITR config request");

    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID, customer);

    checkCompatibleYbVersion(
        universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion);
    if (universe.getUniverseDetails().universePaused) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot perform PITR when the universe is in paused state");
    } else if (universe.getUniverseDetails().updateInProgress) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot perform PITR when the universe is in locked state");
    }

    RestoreSnapshotScheduleParams taskParams =
        parseJsonAndValidate(request, RestoreSnapshotScheduleParams.class);
    if (taskParams.restoreTimeInMillis <= 0L
        || taskParams.restoreTimeInMillis > System.currentTimeMillis()) {
      throw new PlatformServiceException(BAD_REQUEST, "Time to restore specified is incorrect");
    }
    PitrConfig pitrConfig = PitrConfig.getOrBadRequest(taskParams.pitrConfigUUID);
    String masterHostPorts = universe.getMasterAddresses();
    String certificate = universe.getCertificateNodetoNode();
    ListSnapshotSchedulesResponse scheduleResp;
    List<SnapshotScheduleInfo> scheduleInfoList = null;
    YBClient client = null;
    try {
      client = ybClientService.getClient(masterHostPorts, certificate);
      scheduleResp = client.listSnapshotSchedules(taskParams.pitrConfigUUID);
      scheduleInfoList = scheduleResp.getSnapshotScheduleInfoList();
    } catch (Exception ex) {
      log.error(ex.getMessage());
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, ex.getMessage());
    } finally {
      ybClientService.closeClient(client, masterHostPorts);
    }

    if (scheduleInfoList == null || scheduleInfoList.size() != 1) {
      throw new PlatformServiceException(BAD_REQUEST, "Snapshot schedule is invalid");
    }

    taskParams.setUniverseUUID(universeUUID);
    UUID taskUUID = commissioner.submit(TaskType.RestoreSnapshotSchedule, taskParams);
    CustomerTask.create(
        customer,
        universeUUID,
        taskUUID,
        CustomerTask.TargetType.Universe,
        CustomerTask.TaskType.RestoreSnapshotSchedule,
        universe.getName());

    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.Universe,
            universeUUID.toString(),
            Audit.ActionType.RestoreSnapshotSchedule,
            Json.toJson(taskParams),
            taskUUID);
    return new YBPTask(taskUUID).asResult();
  }

  @ApiOperation(
      value = "Delete pitr config on a universe",
      nickname = "deletePitrConfig",
      response = YBPSuccess.class)
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(
                resourceType = ResourceType.UNIVERSE,
                action = Action.BACKUP_RESTORE),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  public Result deletePitrConfig(
      UUID customerUUID, UUID universeUUID, UUID pitrConfigUUID, Http.Request request) {
    // Validate customer UUID
    Customer customer = Customer.getOrBadRequest(customerUUID);
    // Validate universe UUID
    Universe universe = Universe.getOrBadRequest(universeUUID, customer);
    checkCompatibleYbVersion(
        universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion);
    if (universe.getUniverseDetails().universePaused) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot delete PITR config when the universe is in paused state");
    } else if (universe.getUniverseDetails().updateInProgress) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot delete PITR config when the universe is in locked state");
    }
    PitrConfig pitrConfig = PitrConfig.getOrBadRequest(pitrConfigUUID);

    if (pitrConfig.isUsedForXCluster()) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "This PITR config is used for transactional xCluster and cannot be deleted; "
              + "to delete you need to first delete the related xCluster config");
    }

    DeletePitrConfig.Params deletePitrConfigParams = new DeletePitrConfig.Params();
    deletePitrConfigParams.setUniverseUUID(universeUUID);
    deletePitrConfigParams.pitrConfigUuid = pitrConfig.getUuid();

    UUID taskUUID = commissioner.submit(TaskType.DeletePitrConfig, deletePitrConfigParams);
    CustomerTask.create(
        customer,
        universeUUID,
        taskUUID,
        CustomerTask.TargetType.Universe,
        CustomerTask.TaskType.DeletePitrConfig,
        universe.getName());
    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.Universe,
            universeUUID.toString(),
            Audit.ActionType.DeletePitrConfig,
            Json.toJson(pitrConfigUUID));
    return new YBPTask(taskUUID).asResult();
  }

  private void checkCompatibleYbVersion(String ybVersion) {
    if (Util.compareYbVersions(ybVersion, PITR_COMPATIBLE_DB_VERSION, true) < 0) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "PITR feature not supported on universe DB version lower than "
              + PITR_COMPATIBLE_DB_VERSION);
    }
  }

  private void checkCloneCompatibleYbVersion(String ybVersion) {
    if (Util.compareYBVersions(
            ybVersion,
            PITR_CLONE_COMPATIBLE_STABLE_DB_VERSION,
            PITR_CLONE_COMPATIBLE_PREVIEW_DB_VERSION,
            true)
        < 0) {
      String minimumSupportedVersion =
          Util.isStableVersion(ybVersion, true)
              ? PITR_CLONE_COMPATIBLE_STABLE_DB_VERSION
              : PITR_CLONE_COMPATIBLE_PREVIEW_DB_VERSION;
      throw new PlatformServiceException(
          BAD_REQUEST,
          "PITR Clone feature not supported on universe DB version lower than "
              + minimumSupportedVersion);
    }
  }

  private List<PitrConfig> createPitrConfigsWithUnknownState(UUID universeUUID) {
    List<PitrConfig> pitrConfigList = PitrConfig.getByUniverseUUID(universeUUID);
    long currentTimeMillis = System.currentTimeMillis();
    pitrConfigList.stream()
        .forEach(
            p -> {
              p.setState(State.UNKNOWN);
              p.setMinRecoverTimeInMillis(currentTimeMillis);
              p.setMaxRecoverTimeInMillis(currentTimeMillis);
            });
    return pitrConfigList;
  }

  @ApiOperation(
      value = "Create clone of a namespace in a universe",
      nickname = "cloneNamespace",
      response = YBPTask.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "namespaceClone",
          value = "post namespace clone",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.CloneNamespaceParams",
          required = true))
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(
                resourceType = ResourceType.UNIVERSE,
                action = Action.BACKUP_RESTORE),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  public Result cloneNamespace(
      UUID customerUUID,
      UUID universeUUID,
      String tableType,
      String keyspaceName,
      Http.Request request) {
    // Validate customer UUID
    Customer customer = Customer.getOrBadRequest(customerUUID);

    // Validate universe UUID
    Universe universe = Universe.getOrBadRequest(universeUUID, customer);
    if (universe.getUniverseDetails().universePaused) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot clone a namespace when the universe is in paused state");
    } else if (universe.getUniverseDetails().updateInProgress) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot clone a namespace when the universe is in locked state");
    }

    checkCloneCompatibleYbVersion(
        universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion);
    CloneNamespaceParams taskParams = parseJsonAndValidate(request, CloneNamespaceParams.class);

    // Validate that a snapshot schedule exists for the database that needs to be cloned.
    TableType type = BackupUtil.API_TYPE_TO_TABLE_TYPE_MAP.get(ApiType.valueOf(tableType));
    Optional<PitrConfig> pitrConfigOptional = PitrConfig.maybeGet(universeUUID, type, keyspaceName);
    if (!pitrConfigOptional.isPresent()) {
      throw new PlatformServiceException(
          BAD_REQUEST, "PITR Config must be present for the database to be cloned.");
    }
    PitrConfig pitrConfig = pitrConfigOptional.get();
    String masterHostPorts = universe.getMasterAddresses();
    String certificate = universe.getCertificateNodetoNode();
    ListSnapshotSchedulesResponse scheduleResp;
    List<SnapshotScheduleInfo> scheduleInfoList = null;
    YBClient client = null;
    try {
      client = ybClientService.getClient(masterHostPorts, certificate);
      scheduleResp = client.listSnapshotSchedules(pitrConfig.getUuid());
      scheduleInfoList = scheduleResp.getSnapshotScheduleInfoList();
    } catch (Exception ex) {
      log.error(ex.getMessage());
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, ex.getMessage());
    } finally {
      ybClientService.closeClient(client, masterHostPorts);
    }

    if (scheduleInfoList == null || scheduleInfoList.size() != 1) {
      throw new PlatformServiceException(BAD_REQUEST, "Snapshot schedule is invalid");
    }

    long currentTimeMillis = System.currentTimeMillis();
    long minTimeInMillis =
        Math.max(
            currentTimeMillis - pitrConfig.getRetentionPeriod() * 1000L,
            pitrConfig.getCreateTime().getTime());
    if (taskParams.cloneTimeInMillis != null
        && (taskParams.cloneTimeInMillis <= 0L
            || taskParams.cloneTimeInMillis > currentTimeMillis
            || taskParams.cloneTimeInMillis < minTimeInMillis)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Time to clone that has been specified is incorrect");
    }

    BackupUtil.checkApiEnabled(type, universe.getUniverseDetails().getPrimaryCluster().userIntent);

    taskParams.setUniverseUUID(universeUUID);
    taskParams.customerUUID = customerUUID;
    taskParams.tableType = type;
    taskParams.keyspaceName = keyspaceName;
    if (taskParams.cloneTimeInMillis == null) {
      taskParams.cloneTimeInMillis = currentTimeMillis;
    }
    UUID taskUUID = commissioner.submit(TaskType.CloneNamespace, taskParams);
    CustomerTask.create(
        customer,
        universeUUID,
        taskUUID,
        CustomerTask.TargetType.Universe,
        CustomerTask.TaskType.CloneNamespace,
        universe.getName());

    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.Universe,
            universeUUID.toString(),
            Audit.ActionType.CloneNamespace,
            Json.toJson(taskParams),
            taskUUID);
    return new YBPTask(taskUUID).asResult();
  }
}
