// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers.handlers;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.YbcManager;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.UUID;
import org.apache.commons.lang3.StringUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@Singleton
public class YbcHandler {

  private static final Logger LOG = LoggerFactory.getLogger(YbcHandler.class);

  @Inject private Commissioner commissioner;

  @Inject private YbcManager ybcManager;

  public UUID disable(UUID customerUUID, UUID universeUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID);

    UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
    if (!universeDetails.enableYbc || !universeDetails.ybcInstalled) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Ybc is either not installed or enabled on universe: " + universeUUID);
    }

    if (universe.nodesInTransit()) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Cannot disable ybc on universe "
              + universe.universeUUID
              + " as it has nodes in one of "
              + NodeDetails.IN_TRANSIT_STATES
              + " states.");
    }

    UniverseTaskParams taskParams = new UniverseTaskParams();
    taskParams.universeUUID = universeUUID;
    UUID taskUUID = commissioner.submit(TaskType.DisableYbc, taskParams);
    LOG.info(
        "Saved task uuid {} in customer tasks for Disabling Ybc {} for universe {}.",
        taskUUID,
        universeUUID);
    CustomerTask.create(
        customer,
        universeUUID,
        taskUUID,
        CustomerTask.TargetType.Universe,
        CustomerTask.TaskType.DisableYbc,
        universe.name);
    return taskUUID;
  }

  public UUID upgrade(UUID customerUUID, UUID universeUUID, String ybcVersion) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID);
    UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();

    if (!universeDetails.ybcInstalled || !universeDetails.enableYbc) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Ybc is either not installed or enabled on universe: " + universeUUID);
    }

    if (universe.nodesInTransit()) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Cannot perform a ybc upgrade on universe "
              + universe.universeUUID
              + " as it has nodes in one of "
              + NodeDetails.IN_TRANSIT_STATES
              + " states.");
    }

    String targetYbcVersion = ybcManager.getStableYbcVersion();
    if (!StringUtils.isEmpty(ybcVersion)) {
      targetYbcVersion = ybcVersion;
    }

    if (universeDetails.ybcSoftwareVersion.equals(targetYbcVersion)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Ybc version " + targetYbcVersion + " is already present on universe " + universeUUID);
    }

    UniverseDefinitionTaskParams taskParams = universe.getUniverseDetails();
    taskParams.universeUUID = universeUUID;
    taskParams.ybcSoftwareVersion = targetYbcVersion;
    UUID taskUUID = commissioner.submit(TaskType.UpgradeUniverseYbc, taskParams);
    LOG.info(
        "Saved task uuid {} in customer tasks for upgrading Ybc {} for universe {}.",
        taskUUID,
        universeUUID);
    CustomerTask.create(
        customer,
        universeUUID,
        taskUUID,
        CustomerTask.TargetType.Universe,
        CustomerTask.TaskType.UpgradeUniverseYbc,
        universe.name);
    return taskUUID;
  }

  public UUID install(UUID customerUUID, UUID universeUUID, String ybcVersion) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID);

    if (universe.nodesInTransit()) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Cannot perform a ybc installation on universe "
              + universe.universeUUID
              + " as it has nodes in one of "
              + NodeDetails.IN_TRANSIT_STATES
              + " states.");
    }

    String targetYbcVersion = ybcManager.getStableYbcVersion();
    if (!StringUtils.isEmpty(ybcVersion)) {
      targetYbcVersion = ybcVersion;
    }

    if (Util.compareYbVersions(
            universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion,
            Util.YBC_COMPATIBLE_DB_VERSION,
            true)
        < 0) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot install universe with DB version lower than 2.14.0.0-b1");
    }

    UniverseDefinitionTaskParams taskParams = universe.getUniverseDetails();
    taskParams.universeUUID = universeUUID;
    taskParams.ybcSoftwareVersion = targetYbcVersion;
    UUID taskUUID = commissioner.submit(TaskType.InstallYbcSoftware, taskParams);
    LOG.info(
        "Saved task uuid {} in customer tasks for installing Ybc {} for universe {}.",
        taskUUID,
        universeUUID);
    CustomerTask.create(
        customer,
        universeUUID,
        taskUUID,
        CustomerTask.TargetType.Universe,
        CustomerTask.TaskType.InstallYbcSoftware,
        universe.name);
    return taskUUID;
  }
}
