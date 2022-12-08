package com.yugabyte.yw.controllers;

import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.common.BackupUtil;
import com.yugabyte.yw.common.BackupUtil.ApiType;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.CreatePitrConfigParams;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.forms.PlatformResults.YBPTask;
import com.yugabyte.yw.forms.PlatformResults.YBPTasks;
import com.yugabyte.yw.forms.RestoreSnapshotScheduleParams;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.PitrConfig;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.PlatformMetrics;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.Collections;
import java.util.Date;
import java.util.LinkedList;
import java.util.List;
import java.util.Optional;
import java.util.UUID;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiImplicitParam;
import io.swagger.annotations.ApiImplicitParams;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.DeleteSnapshotScheduleResponse;
import org.yb.client.ListSnapshotSchedulesResponse;
import org.yb.client.SnapshotScheduleInfo;
import org.yb.client.SnapshotInfo;
import org.yb.client.YBClient;
import org.yb.CommonTypes.TableType;
import org.yb.master.CatalogEntityInfo.SysSnapshotEntryPB.State;
import play.libs.Json;
import play.mvc.Result;

@Api(
    value = "PITR management",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
@Slf4j
public class PitrController extends AuthenticatedController {

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
  public Result createPitrConfig(
      UUID customerUUID, UUID universeUUID, String tableType, String keyspaceName) {
    // Validate customer UUID
    Customer customer = Customer.getOrBadRequest(customerUUID);

    // Validate universe UUID
    Universe universe = Universe.getOrBadRequest(universeUUID);
    if (universe.getUniverseDetails().universePaused) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot enable PITR when the universe is in paused state");
    }
    CreatePitrConfigParams taskParams = parseJsonAndValidate(CreatePitrConfigParams.class);

    if (taskParams.retentionPeriodInSeconds <= 0L) {
      throw new PlatformServiceException(
          BAD_REQUEST, "PITR Config retention period can't be less than 1 second");
    }

    if (taskParams.retentionPeriodInSeconds <= taskParams.intervalInSeconds) {
      throw new PlatformServiceException(
          BAD_REQUEST, "PITR Config interval can't be less than retention period");
    }

    TableType type = BackupUtil.API_TYPE_TO_TABLE_TYPE_MAP.get(ApiType.valueOf(tableType));
    Optional<PitrConfig> pitrConfig = PitrConfig.maybeGet(universeUUID, type, keyspaceName);
    if (pitrConfig.isPresent()) {
      throw new PlatformServiceException(BAD_REQUEST, "PITR Config is already present");
    }

    taskParams.universeUUID = universeUUID;
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
        universe.name);

    auditService()
        .createAuditEntryWithReqBody(
            ctx(),
            Audit.TargetType.Universe,
            universeUUID.toString(),
            Audit.ActionType.CreatePitrConfig,
            Json.toJson(taskParams),
            taskUUID);
    return new YBPTask(taskUUID).asResult();
  }

  @ApiOperation(
      value = "List the PITR configs of a universe",
      response = PitrConfig.class,
      responseContainer = "List",
      nickname = "ListOfPitrConfigs")
  public Result listPitrConfigs(UUID customerUUID, UUID universeUUID) {
    // Validate customer UUID
    Customer customer = Customer.getOrBadRequest(customerUUID);

    // Validate universe UUID
    Universe universe = Universe.getOrBadRequest(universeUUID);

    List<PitrConfig> pitrConfigList = new LinkedList<>();
    String masterHostPorts = universe.getMasterAddresses();
    String certificate = universe.getCertificateNodetoNode();
    YBClient client = null;
    ListSnapshotSchedulesResponse scheduleResp;
    List<SnapshotScheduleInfo> scheduleInfoList = null;

    if (universe.getUniverseDetails().universePaused) {
      pitrConfigList = PitrConfig.getByUniverseUUID(universeUUID);
      long currentTimeMillis = System.currentTimeMillis();
      pitrConfigList
          .stream()
          .forEach(
              p -> {
                p.setState(State.UNKNOWN);
                p.setMinRecoverTimeInMillis(currentTimeMillis);
                p.setMaxRecoverTimeInMillis(currentTimeMillis);
              });
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

      for (SnapshotScheduleInfo snapshotScheduleInfo : scheduleInfoList) {
        PitrConfig pitrConfig = PitrConfig.get(snapshotScheduleInfo.getSnapshotScheduleUUID());
        if (pitrConfig == null) {
          continue;
        }
        boolean pitrStatus =
            BackupUtil.allSnapshotsSuccessful(snapshotScheduleInfo.getSnapshotInfoList());
        long currentTimeMillis = System.currentTimeMillis();
        long minTimeInMillis =
            Math.max(
                currentTimeMillis - pitrConfig.getRetentionPeriod() * 1000L,
                pitrConfig.getCreateTime().getTime());
        pitrConfig.setMinRecoverTimeInMillis(minTimeInMillis);
        pitrConfig.setMaxRecoverTimeInMillis(currentTimeMillis);
        pitrConfig.setState(pitrStatus ? State.COMPLETE : State.FAILED);
        pitrConfigList.add(pitrConfig);
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
  public Result restore(UUID customerUUID, UUID universeUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID);
    if (universe.getUniverseDetails().universePaused) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot perform PITR when the universe is in paused state");
    }

    RestoreSnapshotScheduleParams taskParams =
        parseJsonAndValidate(RestoreSnapshotScheduleParams.class);
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

    taskParams.universeUUID = universeUUID;
    UUID taskUUID = commissioner.submit(TaskType.RestoreSnapshotSchedule, taskParams);
    CustomerTask.create(
        customer,
        universeUUID,
        taskUUID,
        CustomerTask.TargetType.Universe,
        CustomerTask.TaskType.RestoreSnapshotSchedule,
        universe.name);

    auditService()
        .createAuditEntryWithReqBody(
            ctx(),
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
  public Result deletePitrConfig(UUID customerUUID, UUID universeUUID, UUID pitrConfigUUID) {
    // Validate customer UUID
    Customer customer = Customer.getOrBadRequest(customerUUID);

    // Validate universe UUID
    Universe universe = Universe.getOrBadRequest(universeUUID);
    if (universe.getUniverseDetails().universePaused) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot delete PITR config when the universe is in paused state");
    }
    PitrConfig pitrConfig = PitrConfig.getOrBadRequest(pitrConfigUUID);

    DeleteSnapshotScheduleResponse resp = null;
    YBClient client = null;
    String masterHostPorts = universe.getMasterAddresses();
    String certificate = universe.getCertificateNodetoNode();

    try {
      client = ybClientService.getClient(masterHostPorts, certificate);
      ListSnapshotSchedulesResponse scheduleListResp = client.listSnapshotSchedules(null);
      for (SnapshotScheduleInfo scheduleInfo : scheduleListResp.getSnapshotScheduleInfoList()) {
        if (scheduleInfo.getSnapshotScheduleUUID().equals(pitrConfigUUID)) {
          resp = client.deleteSnapshotSchedule(pitrConfigUUID);
        }
      }

    } catch (Exception e) {
      log.error("Hit exception : {}", e.getMessage());
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, e.getMessage());
    } finally {
      ybClientService.closeClient(client, masterHostPorts);
    }

    if (resp.hasError()) {
      String errorMsg = "Failed due to error: " + resp.errorMessage();
      log.error(errorMsg);
      throw new RuntimeException(errorMsg);
    }

    pitrConfig.delete();

    auditService()
        .createAuditEntryWithReqBody(
            ctx(),
            Audit.TargetType.Universe,
            universeUUID.toString(),
            Audit.ActionType.DeletePitrConfig,
            Json.toJson(pitrConfigUUID));
    return YBPSuccess.empty();
  }
}
