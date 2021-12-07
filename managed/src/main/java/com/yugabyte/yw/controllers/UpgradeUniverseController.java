// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import com.google.inject.Inject;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.controllers.handlers.UpgradeUniverseHandler;
import com.yugabyte.yw.forms.CertsRotateParams;
import com.yugabyte.yw.forms.GFlagsUpgradeParams;
import com.yugabyte.yw.forms.SoftwareUpgradeParams;
import com.yugabyte.yw.forms.SystemdUpgradeParams;
import com.yugabyte.yw.forms.TlsToggleParams;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.forms.VMImageUpgradeParams;
import com.yugabyte.yw.forms.PlatformResults.YBPTask;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import play.mvc.Result;

@Slf4j
public class UpgradeUniverseController extends AuthenticatedController {

  @Inject UpgradeUniverseHandler upgradeUniverseHandler;

  @Inject RuntimeConfigFactory runtimeConfigFactory;

  /**
   * API that restarts all nodes in the universe. Supports rolling and non-rolling restart
   *
   * @param customerUuid ID of customer
   * @param universeUuid ID of universe
   * @return Result of update operation with task id
   */
  public Result restartUniverse(UUID customerUuid, UUID universeUuid) {
    return requestHandler(
        upgradeUniverseHandler::restartUniverse,
        UpgradeTaskParams.class,
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
  public Result upgradeSoftware(UUID customerUuid, UUID universeUuid) {
    return requestHandler(
        upgradeUniverseHandler::upgradeSoftware,
        SoftwareUpgradeParams.class,
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
  public Result upgradeGFlags(UUID customerUuid, UUID universeUuid) {
    return requestHandler(
        upgradeUniverseHandler::upgradeGFlags,
        GFlagsUpgradeParams.class,
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
  public Result upgradeCerts(UUID customerUuid, UUID universeUuid) {
    return requestHandler(
        upgradeUniverseHandler::rotateCerts, CertsRotateParams.class, customerUuid, universeUuid);
  }

  /**
   * API that toggles TLS state of the universe. Can enable/disable node to node and client to node
   * encryption. Supports rolling and non-rolling upgrade of the universe.
   *
   * @param customerUuid ID of customer
   * @param universeUuid ID of universe
   * @return Result of update operation with task id
   */
  public Result upgradeTls(UUID customerUuid, UUID universeUuid) {
    return requestHandler(
        upgradeUniverseHandler::toggleTls, TlsToggleParams.class, customerUuid, universeUuid);
  }

  /**
   * API that upgrades VM Image for AWS and GCP based universes. Supports only rolling upgrade of
   * the universe.
   *
   * @param customerUuid ID of customer
   * @param universeUuid ID of universe
   * @return Result of update operation with task id
   */
  public Result upgradeVMImage(UUID customerUuid, UUID universeUuid) {
    Customer customer = Customer.getOrBadRequest(customerUuid);
    Universe universe = Universe.getValidUniverseOrBadRequest(universeUuid, customer);

    if (!runtimeConfigFactory.forUniverse(universe).getBoolean("yb.cloud.enabled")) {
      throw new PlatformServiceException(METHOD_NOT_ALLOWED, "VM image upgrade is disabled.");
    }

    return requestHandler(
        upgradeUniverseHandler::upgradeVMImage,
        VMImageUpgradeParams.class,
        customerUuid,
        universeUuid);
  }

  /**
   * API that upgrades from cron to systemd for universes. Supports only rolling upgrade of the
   * universe.
   *
   * @param customerUuid ID of customer
   * @param universeUuid ID of universe
   * @return Result of update operation with task id
   */
  public Result upgradeSystemd(UUID customerUUID, UUID universeUUID) {
    return requestHandler(
        upgradeUniverseHandler::upgradeSystemd,
        SystemdUpgradeParams.class,
        customerUUID,
        universeUUID);
  }

  private <T extends UpgradeTaskParams> Result requestHandler(
      IUpgradeUniverseHandlerMethod<T> serviceMethod,
      Class<T> type,
      UUID customerUuid,
      UUID universeUuid) {
    Customer customer = Customer.getOrBadRequest(customerUuid);
    Universe universe = Universe.getValidUniverseOrBadRequest(universeUuid, customer);
    T requestParams =
        UniverseControllerRequestBinder.bindFormDataToUpgradeTaskParams(request(), type);

    log.info(
        "Upgrade for universe {} [ {} ] customer {}.",
        universe.name,
        universe.universeUUID,
        customer.uuid);

    UUID taskUuid = serviceMethod.upgrade(requestParams, customer, universe);
    auditService().createAuditEntryWithReqBody(ctx(), taskUuid);
    return new YBPTask(taskUuid, universe.universeUUID).asResult();
  }
}
