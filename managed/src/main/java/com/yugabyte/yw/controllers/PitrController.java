// Copyright (c) YugabyteDB, Inc.
package com.yugabyte.yw.controllers;

import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.backuprestore.BackupUtil;
import com.yugabyte.yw.common.pitr.PitrConfigHelper;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.CloneNamespaceParams;
import com.yugabyte.yw.forms.CreatePitrConfigParams;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPTask;
import com.yugabyte.yw.forms.RestoreSnapshotScheduleParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.SoftwareUpgradeState;
import com.yugabyte.yw.forms.UpdatePitrConfigParams;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.PitrConfig;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.common.YbaApi;
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
import java.util.UUID;
import java.util.concurrent.locks.Lock;
import java.util.concurrent.locks.ReentrantLock;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
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

  public static final String PITR_CLONE_COMPATIBLE_PREVIEW_DB_VERSION = "2.25.1.0-b1";
  public static final String PITR_CLONE_COMPATIBLE_STABLE_DB_VERSION = "2024.2.0.0-b1";

  private static final Lock listPitrConfigsLock = new ReentrantLock();

  private final Commissioner commissioner;
  private final YBClientService ybClientService;
  private final PitrConfigHelper pitrConfigHelper;

  @Inject
  public PitrController(
      Commissioner commissioner,
      YBClientService ybClientService,
      PitrConfigHelper pitrConfigHelper) {
    this.commissioner = commissioner;
    this.ybClientService = ybClientService;
    this.pitrConfigHelper = pitrConfigHelper;
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

    CreatePitrConfigParams taskParams = parseJsonAndValidate(request, CreatePitrConfigParams.class);
    UUID taskUUID =
        pitrConfigHelper.createPitrConfig(
            customerUUID, universeUUID, tableType, keyspaceName, taskParams);
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
      notes = "WARNING: This is a preview API that could change.",
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
  @YbaApi(visibility = YbaApi.YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.25.1.0")
  public Result updatePitrConfig(
      UUID customerUUID, UUID universeUUID, UUID pitrConfigUUID, Http.Request request) {

    UpdatePitrConfigParams taskParams = parseJsonAndValidate(request, UpdatePitrConfigParams.class);
    UUID taskUUID =
        pitrConfigHelper.updatePitrConfig(customerUUID, universeUUID, pitrConfigUUID, taskParams);
    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.Universe,
            universeUUID.toString(),
            Audit.ActionType.UpdatePitrConfig,
            Json.toJson(taskParams),
            taskUUID);
    return new YBPTask(taskUUID, pitrConfigUUID).asResult();
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

    // Serialize PITR config reads. If the master leader is down, reads can take ~2 minutes to
    // time out; allowing concurrent reads during that window can exhaust the backend thread
    // pool and freeze the UI.
    if (!listPitrConfigsLock.tryLock()) {
      throw new PlatformServiceException(
          TOO_MANY_REQUESTS,
          "Another PITR config list request is already in progress. Please retry shortly.");
    }
    try {
      List<PitrConfig> pitrConfigList = new LinkedList<>();
      ListSnapshotSchedulesResponse scheduleResp;
      List<SnapshotScheduleInfo> scheduleInfoList = null;

      pitrConfigHelper.checkCompatibleYbVersion(
          universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion);
      if (universe.getUniverseDetails().universePaused) {
        pitrConfigList = createPitrConfigsWithUnknownState(universeUUID);
      } else {
        try (YBClient client = ybClientService.getUniverseClient(universe)) {
          scheduleResp = client.listSnapshotSchedules(null);
          scheduleInfoList = scheduleResp.getSnapshotScheduleInfoList();
        } catch (Exception ex) {
          log.error(ex.getMessage());
          throw new PlatformServiceException(INTERNAL_SERVER_ERROR, ex.getMessage());
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
                    snapshotScheduleInfo.getSnapshotInfoList(), pitrConfig);
            pitrConfig.setMinRecoverTimeInMillis(minTimeInMillis);
            pitrConfig.setMaxRecoverTimeInMillis(currentTimeMillis);
            pitrConfig.setState(pitrStatus ? State.COMPLETE : State.FAILED);
            pitrConfigList.add(pitrConfig);
          }
        }
      }
      return PlatformResults.withData(pitrConfigList);
    } finally {
      listPitrConfigsLock.unlock();
    }
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

    pitrConfigHelper.checkCompatibleYbVersion(
        universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion);
    if (universe.getUniverseDetails().universePaused) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot perform PITR when the universe is in paused state");
    } else if (universe.getUniverseDetails().updateInProgress) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot perform PITR when the universe is in locked state");
    }

    if (!universe.getUniverseDetails().softwareUpgradeState.equals(SoftwareUpgradeState.Ready)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot perform PITR when the universe is not in ready state");
    }

    RestoreSnapshotScheduleParams taskParams =
        parseJsonAndValidate(request, RestoreSnapshotScheduleParams.class);
    if (taskParams.restoreTimeInMillis <= 0L
        || taskParams.restoreTimeInMillis > System.currentTimeMillis()) {
      throw new PlatformServiceException(BAD_REQUEST, "Time to restore specified is incorrect");
    }
    PitrConfig pitrConfig = PitrConfig.getOrBadRequest(taskParams.pitrConfigUUID);
    ListSnapshotSchedulesResponse scheduleResp;
    List<SnapshotScheduleInfo> scheduleInfoList = null;
    try (YBClient client = ybClientService.getUniverseClient(universe)) {
      scheduleResp = client.listSnapshotSchedules(taskParams.pitrConfigUUID);
      scheduleInfoList = scheduleResp.getSnapshotScheduleInfoList();
    } catch (Exception ex) {
      log.error(ex.getMessage());
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, ex.getMessage());
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
    return new YBPTask(taskUUID, pitrConfig.getUuid()).asResult();
  }

  @ApiOperation(
      value = "Delete pitr config on a universe",
      nickname = "deletePitrConfig",
      response = YBPTask.class)
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
    UUID taskUUID = pitrConfigHelper.deletePitrConfig(customerUUID, universeUUID, pitrConfigUUID);
    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.Universe,
            universeUUID.toString(),
            Audit.ActionType.DeletePitrConfig,
            Json.toJson(pitrConfigUUID));
    return new YBPTask(taskUUID, pitrConfigUUID).asResult();
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
      notes = "YbaApi Internal.",
      value = "Clone namespace via PITR on a universe",
      nickname = "cloneNamespace",
      response = YBPTask.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "namespaceClone",
          value = "perform clone via PITR",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.CloneNamespaceParams",
          required = true))
  @YbaApi(visibility = YbaApi.YbaApiVisibility.INTERNAL, sinceYBAVersion = "2025.1.0.0")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(
                resourceType = ResourceType.UNIVERSE,
                action = Action.BACKUP_RESTORE),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  public Result cloneNamespace(UUID customerUUID, UUID universeUUID, Http.Request request) {
    log.info("Received clone via PITR config request");
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

    if (!universe.getUniverseDetails().softwareUpgradeState.equals(SoftwareUpgradeState.Ready)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot clone namespace when the universe is not in ready state");
    }

    checkCloneCompatibleYbVersion(
        universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion);
    CloneNamespaceParams taskParams = parseJsonAndValidate(request, CloneNamespaceParams.class);

    // Validate that a snapshot schedule exists for the database that needs to be cloned.
    PitrConfig pitrConfig = PitrConfig.getOrBadRequest(taskParams.pitrConfigUUID);
    ListSnapshotSchedulesResponse scheduleResp;
    List<SnapshotScheduleInfo> scheduleInfoList = null;
    try (YBClient client = ybClientService.getUniverseClient(universe)) {
      scheduleResp = client.listSnapshotSchedules(pitrConfig.getUuid());
      scheduleInfoList = scheduleResp.getSnapshotScheduleInfoList();
    } catch (Exception ex) {
      log.error(ex.getMessage());
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, ex.getMessage());
    }

    if (scheduleInfoList == null || scheduleInfoList.size() != 1) {
      throw new PlatformServiceException(BAD_REQUEST, "Snapshot schedule is invalid");
    }

    if (StringUtils.isBlank(taskParams.targetKeyspaceName)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Name of the cloned database must not be empty.");
    }

    long currentTimeMillis = System.currentTimeMillis();
    long minTimeInMillis =
        BackupUtil.getMinRecoveryTimeForSchedule(
            scheduleInfoList.get(0).getSnapshotInfoList(), pitrConfig);
    if (taskParams.cloneTimeInMillis != null
        && (taskParams.cloneTimeInMillis < minTimeInMillis
            || taskParams.cloneTimeInMillis > currentTimeMillis)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Time to clone that has been specified is incorrect");
    }

    BackupUtil.checkApiEnabled(
        pitrConfig.getTableType(), universe.getUniverseDetails().getPrimaryCluster().userIntent);

    taskParams.setUniverseUUID(universeUUID);
    taskParams.setKeyspaceName(pitrConfig.getDbName());
    taskParams.setTableType(pitrConfig.getTableType());
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
