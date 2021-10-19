/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.controllers.handlers;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.XClusterReplicationFormData;
import com.yugabyte.yw.forms.XClusterReplicationTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.Collections;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.yb.util.NetUtil;
import static play.mvc.Http.Status.BAD_REQUEST;

@Slf4j
public class XClusterReplicationHandler {

  private final Commissioner commissioner;

  @Inject
  public XClusterReplicationHandler(Commissioner commissioner) {
    this.commissioner = commissioner;
  }

  public UUID createReplication(
      Customer customer, XClusterReplicationFormData formData, UUID targetUniverseUUID) {
    log.info(
        "Create xCluster replication, customer uuid: {}, source universe: {}, target universe: {} "
            + "[source universe tables {}]",
        customer.uuid,
        formData.sourceUniverseUUID,
        targetUniverseUUID,
        formData.sourceTableIds);

    XClusterReplicationTaskParams params = getParams(formData, targetUniverseUUID);
    UUID taskUUID = commissioner.submit(TaskType.CreateXClusterReplication, params);

    log.info(
        "Submitted create xCluster replication for source universe {}, target universe {}, "
            + "task uuid = {}",
        params.sourceUniverseUUID,
        params.targetUniverseUUID,
        taskUUID);

    // Add this task uuid to the target universe.
    Universe target = Universe.getValidUniverseOrBadRequest(params.targetUniverseUUID, customer);
    CustomerTask.create(
        customer,
        params.targetUniverseUUID,
        taskUUID,
        CustomerTask.TargetType.Universe,
        CustomerTask.TaskType.CreateXClusterReplication,
        target.name);

    log.info(
        "Created xCluster replication config between source universe {} "
            + "and target universe {} for customer [{}]",
        params.sourceUniverseUUID,
        params.targetUniverseUUID,
        customer.name);

    return taskUUID;
  }

  public UUID editReplication(
      Customer customer, XClusterReplicationFormData formData, UUID targetUniverseUUID) {
    log.info(
        "Edit xCluster replication, customer uuid: {}, source universe: {}, target universe: {}, "
            + "source tables added: {}, source tables removed: {}, "
            + "source master addresses changed: {}",
        customer.uuid,
        formData.sourceUniverseUUID,
        targetUniverseUUID,
        formData.sourceTableIdsToAdd,
        formData.sourceTableIdsToRemove,
        formData.sourceMasterAddressesToChange);

    XClusterReplicationTaskParams params = getParams(formData, targetUniverseUUID);
    UUID taskUUID = commissioner.submit(TaskType.EditXClusterReplication, params);

    log.info(
        "Submitted edit xCluster replication for source universe {}, target universe {}, "
            + "task uuid = {}",
        params.sourceUniverseUUID,
        params.targetUniverseUUID,
        taskUUID);

    // Add this task uuid to the target universe.
    Universe target = Universe.getValidUniverseOrBadRequest(params.targetUniverseUUID, customer);
    CustomerTask.create(
        customer,
        params.targetUniverseUUID,
        taskUUID,
        CustomerTask.TargetType.Universe,
        CustomerTask.TaskType.EditXClusterReplication,
        target.name);

    log.info(
        "Edited xCluster replication universe between source universe {} "
            + "and target universe {} for customer [{}]",
        params.sourceUniverseUUID,
        params.targetUniverseUUID,
        customer.name);

    return taskUUID;
  }

  public UUID deleteReplication(
      Customer customer, XClusterReplicationFormData formData, UUID targetUniverseUUID) {
    log.info(
        "Delete xCluster replication, customer uuid: {}, source universe: {}, target universe: {}",
        customer.uuid,
        formData.sourceUniverseUUID,
        targetUniverseUUID);

    XClusterReplicationTaskParams params = getParams(formData, targetUniverseUUID);
    UUID taskUUID = commissioner.submit(TaskType.DeleteXClusterReplication, params);

    log.info(
        "Submitted delete xCluster replication for source universe {}, target universe {}, "
            + "task uuid = {}",
        params.sourceUniverseUUID,
        params.targetUniverseUUID,
        taskUUID);

    // Add this task uuid to the target universe.
    Universe target = Universe.getValidUniverseOrBadRequest(params.targetUniverseUUID, customer);
    CustomerTask.create(
        customer,
        params.targetUniverseUUID,
        taskUUID,
        CustomerTask.TargetType.Universe,
        CustomerTask.TaskType.DeleteXClusterReplication,
        target.name);

    log.info(
        "Deleted xCluster replication universe between source universe {} "
            + "and target universe {} for customer [{}]",
        params.sourceUniverseUUID,
        params.targetUniverseUUID,
        customer.name);

    return taskUUID;
  }

  public UUID pauseReplication(
      Customer customer, XClusterReplicationFormData formData, UUID targetUniverseUUID) {
    log.info(
        "Pause xCluster replication, customer uuid: {}, source universe: {}, target universe: {}",
        customer.uuid,
        formData.sourceUniverseUUID,
        targetUniverseUUID);

    XClusterReplicationTaskParams params = getParams(formData, targetUniverseUUID);
    params.active = false;

    UUID taskUUID = commissioner.submit(TaskType.PauseOrResumeXClusterReplication, params);

    log.info(
        "Submitted pause xCluster replication for source universe {}, target universe {}, "
            + "task uuid = {}",
        params.sourceUniverseUUID,
        params.targetUniverseUUID,
        taskUUID);

    // Add this task uuid to the target universe.
    Universe target = Universe.getValidUniverseOrBadRequest(params.targetUniverseUUID, customer);
    CustomerTask.create(
        customer,
        params.targetUniverseUUID,
        taskUUID,
        CustomerTask.TargetType.Universe,
        CustomerTask.TaskType.PauseXClusterReplication,
        target.name);

    log.info(
        "Paused xCluster replication universe between source universe {} "
            + "and target universe {} for customer [{}]",
        params.sourceUniverseUUID,
        params.targetUniverseUUID,
        customer.name);

    return taskUUID;
  }

  public UUID resumeReplication(
      Customer customer, XClusterReplicationFormData formData, UUID targetUniverseUUID) {
    log.info(
        "Resume xCluster replication, customer uuid: {}, source universe: {}, target universe: {}",
        customer.uuid,
        formData.sourceUniverseUUID,
        targetUniverseUUID);

    XClusterReplicationTaskParams params = getParams(formData, targetUniverseUUID);
    params.active = true;

    UUID taskUUID = commissioner.submit(TaskType.PauseOrResumeXClusterReplication, params);

    log.info(
        "Submitted resume xCluster replication for source universe {}, target universe {}, "
            + "task uuid = {}",
        params.sourceUniverseUUID,
        params.targetUniverseUUID,
        taskUUID);

    // Add this task uuid to the target universe.
    Universe target = Universe.getValidUniverseOrBadRequest(params.targetUniverseUUID, customer);
    CustomerTask.create(
        customer,
        params.targetUniverseUUID,
        taskUUID,
        CustomerTask.TargetType.Universe,
        CustomerTask.TaskType.ResumeXClusterReplication,
        target.name);

    log.info(
        "Resumed xCluster replication universe between source universe {} "
            + "and target universe {} for customer [{}]",
        params.sourceUniverseUUID,
        params.targetUniverseUUID,
        customer.name);

    return taskUUID;
  }

  private XClusterReplicationTaskParams getParams(
      XClusterReplicationFormData formData, UUID targetUniverseUUID) {
    XClusterReplicationTaskParams params = new XClusterReplicationTaskParams();
    params.sourceUniverseUUID = formData.sourceUniverseUUID;
    params.targetUniverseUUID = targetUniverseUUID;
    // Since xCluster replication is a pull based mechanism,
    // the universe context will be the target universe
    params.universeUUID = targetUniverseUUID;
    params.expectedUniverseVersion = -1;

    params.sourceTableIDs =
        formData.sourceTableIds == null ? Collections.emptyList() : formData.sourceTableIds;

    params.bootstrapIDs =
        formData.bootstrapIds == null ? Collections.emptyList() : formData.bootstrapIds;

    params.sourceTableIdsToAdd =
        formData.sourceTableIdsToAdd == null
            ? Collections.emptyList()
            : formData.sourceTableIdsToAdd;

    params.sourceTableIdsToRemove =
        formData.sourceTableIdsToRemove == null
            ? Collections.emptyList()
            : formData.sourceTableIdsToRemove;

    try {
      params.sourceMasterAddresses =
          formData.sourceMasterAddressesToChange == null
              ? Collections.emptyList()
              : NetUtil.parseStringsAsPB(String.join(",", formData.sourceMasterAddressesToChange));
    } catch (Exception e) {
      throw new PlatformServiceException(BAD_REQUEST, e.getMessage());
    }

    return params;
  }
}
