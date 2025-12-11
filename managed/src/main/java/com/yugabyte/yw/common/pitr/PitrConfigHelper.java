// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.pitr;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.google.inject.Singleton;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.tasks.DeletePitrConfig;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.backuprestore.BackupUtil;
import com.yugabyte.yw.common.backuprestore.BackupUtil.ApiType;
import com.yugabyte.yw.forms.CreatePitrConfigParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.SoftwareUpgradeState;
import com.yugabyte.yw.forms.UpdatePitrConfigParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.PitrConfig;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.Optional;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.CommonTypes.TableType;

@Slf4j
@Singleton
public class PitrConfigHelper {

  public static final String PITR_COMPATIBLE_DB_VERSION = "2.14.0.0-b1";
  private final Commissioner commissioner;

  @Inject
  public PitrConfigHelper(Commissioner commissioner) {
    this.commissioner = commissioner;
  }

  public UUID createPitrConfig(
      UUID customerUUID,
      UUID universeUUID,
      String tableType,
      String keyspaceName,
      CreatePitrConfigParams taskParams) {
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

    if (!universe.getUniverseDetails().softwareUpgradeState.equals(SoftwareUpgradeState.Ready)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot enable PITR when the universe is not in ready state");
    }

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
    return taskUUID;
  }

  public UUID updatePitrConfig(
      UUID customerUUID,
      UUID universeUUID,
      UUID pitrConfigUUID,
      UpdatePitrConfigParams taskParams) {
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

    if (!universe.getUniverseDetails().softwareUpgradeState.equals(SoftwareUpgradeState.Ready)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot update PITR when the universe is not in ready state");
    }

    PitrConfig pitrConfig = PitrConfig.getOrBadRequest(pitrConfigUUID);

    checkCompatibleYbVersion(
        universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion);

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
    return taskUUID;
  }

  public UUID deletePitrConfig(UUID customerUUID, UUID universeUUID, UUID pitrConfigUUID) {
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
    return taskUUID;
  }

  public void checkCompatibleYbVersion(String ybVersion) {
    if (Util.compareYbVersions(ybVersion, PITR_COMPATIBLE_DB_VERSION, true) < 0) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "PITR feature not supported on universe DB version lower than "
              + PITR_COMPATIBLE_DB_VERSION);
    }
  }
}
