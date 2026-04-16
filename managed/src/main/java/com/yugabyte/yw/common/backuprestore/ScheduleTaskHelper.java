// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.backuprestore;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.BackupRequestParams;
import com.yugabyte.yw.forms.backuprestore.BackupScheduleTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.Schedule;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import java.io.IOException;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import play.libs.Json;

@Slf4j
@Singleton
public class ScheduleTaskHelper {

  private final Commissioner commissioner;

  @Inject
  public ScheduleTaskHelper(Commissioner commissioner) {
    this.commissioner = commissioner;
  }

  public UUID createDeleteScheduledBackupTask(
      Schedule schedule, UUID universeUUID, Customer customer) {
    if (!schedule.getOwnerUUID().equals(universeUUID)) {
      throw new PlatformServiceException(BAD_REQUEST, "Schedule not owned by Universe.");
    }
    BackupRequestParams scheduleParams =
        Json.fromJson(schedule.getTaskParams(), BackupRequestParams.class);
    Universe universe = Universe.getOrBadRequest(universeUUID);
    if (schedule.isRunningState()
        || (scheduleParams.enablePointInTimeRestore && universe.universeIsLocked())) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot delete schedule as Universe is locked.");
    }
    ObjectMapper mapper = new ObjectMapper();
    BackupScheduleTaskParams taskParams = null;
    try {
      taskParams =
          mapper.readValue(
              mapper.writeValueAsString(universe.getUniverseDetails()),
              BackupScheduleTaskParams.class);
    } catch (IOException e) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Failed while processing delete schedule task params: " + e.getMessage());
    }
    taskParams.setCustomerUUID(customer.getUuid());
    taskParams.setScheduleUUID(schedule.getScheduleUUID());
    taskParams.setScheduleParams(scheduleParams);

    TaskType taskType =
        universe
                .getUniverseDetails()
                .getPrimaryCluster()
                .userIntent
                .providerType
                .equals(CloudType.kubernetes)
            ? TaskType.DeleteBackupScheduleKubernetes
            : TaskType.DeleteBackupSchedule;
    UUID taskUUID = commissioner.submit(taskType, taskParams);
    log.info("Submitted task to universe {}, task uuid = {}.", universe.getName(), taskUUID);
    CustomerTask.create(
        customer,
        universe.getUniverseUUID(),
        taskUUID,
        CustomerTask.TargetType.Schedule,
        CustomerTask.TaskType.Delete,
        schedule.getScheduleName());
    log.info("Saved task uuid {} in customer tasks for universe {}", taskUUID, universe.getName());
    return taskUUID;
  }

  public UUID createCreateScheduledBackupTask(
      BackupScheduleTaskParams taskParams, Customer customer, Universe universe) {
    TaskType taskType =
        universe
                .getUniverseDetails()
                .getPrimaryCluster()
                .userIntent
                .providerType
                .equals(CloudType.kubernetes)
            ? TaskType.CreateBackupScheduleKubernetes
            : TaskType.CreateBackupSchedule;
    UUID taskUUID = commissioner.submit(taskType, taskParams);
    log.info("Submitted task to universe {}, task uuid = {}.", universe.getName(), taskUUID);
    CustomerTask.create(
        customer,
        taskParams.getUniverseUUID(),
        taskUUID,
        CustomerTask.TargetType.Schedule,
        CustomerTask.TaskType.Create,
        universe.getName());
    log.info("Saved task uuid {} in customer tasks for universe {}", taskUUID, universe.getName());
    return taskUUID;
  }

  public UUID createEditScheduledBackupTask(
      BackupScheduleTaskParams taskParams,
      Customer customer,
      Universe universe,
      Schedule schedule) {
    TaskType taskType =
        universe
                .getUniverseDetails()
                .getPrimaryCluster()
                .userIntent
                .providerType
                .equals(CloudType.kubernetes)
            ? TaskType.EditBackupScheduleKubernetes
            : TaskType.EditBackupSchedule;
    UUID taskUUID = commissioner.submit(taskType, taskParams);
    log.info("Submitted task to universe {}, task uuid = {}.", universe.getName(), taskUUID);
    CustomerTask.create(
        customer,
        universe.getUniverseUUID(),
        taskUUID,
        CustomerTask.TargetType.Schedule,
        CustomerTask.TaskType.Update,
        schedule.getScheduleName());
    log.info("Saved task uuid {} in customer tasks for universe {}", taskUUID, universe.getName());
    return taskUUID;
  }
}
