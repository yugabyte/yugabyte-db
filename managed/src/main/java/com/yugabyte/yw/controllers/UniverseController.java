// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import com.google.inject.Inject;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.controllers.handlers.UniverseCRUDHandler;
import com.yugabyte.yw.forms.*;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import io.swagger.annotations.*;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.mvc.Result;

import java.util.Collections;
import java.util.Optional;
import java.util.UUID;

import static com.yugabyte.yw.controllers.UniverseControllerRequestBinder.bindFormDataToTaskParams;

@Api(value = "Universe", authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class UniverseController extends AuthenticatedController {
  private static final Logger LOG = LoggerFactory.getLogger(UniverseController.class);

  @Inject private RuntimeConfigFactory runtimeConfigFactory;

  @Inject private UniverseCRUDHandler universeCRUDHandler;

  /**
   * Find universe with name filter.
   *
   * @return List of Universe UUID
   * @deprecated Use universe list with name parameter
   */
  @Deprecated
  public Result find(UUID customerUUID, String name) {
    // Verify the customer with this universe is present.
    Customer.getOrBadRequest(customerUUID);
    LOG.info("Finding Universe with name {}.", name);
    Optional<Universe> universe = Universe.maybeGetUniverseByName(name);
    if (universe.isPresent()) {
      return YWResults.withData(Collections.singletonList(universe.get().universeUUID));
    }
    return YWResults.withData(Collections.emptyList());
  }

  /**
   * API that binds the UniverseDefinitionTaskParams class by merging the UserIntent with the
   * generated taskParams.
   *
   * @param customerUUID the ID of the customer configuring the Universe.
   * @return UniverseDefinitionTasksParams in a serialized form
   */
  @ApiOperation(
      value = "configure the universe parameters",
      notes =
          "This API builds the new universe definition task parameters by merging the input "
              + "UserIntent with the current taskParams and returns the resulting task parameters "
              + "in a serialized form",
      response = UniverseDefinitionTaskParams.class)
  public Result configure(UUID customerUUID) {

    // Verify the customer with this universe is present.
    Customer customer = Customer.getOrBadRequest(customerUUID);

    UniverseConfigureTaskParams taskParams =
        bindFormDataToTaskParams(request(), UniverseConfigureTaskParams.class);

    universeCRUDHandler.configure(customer, taskParams);

    return YWResults.withData(taskParams);
  }

  /**
   * API that queues a task to create a new universe. This does not wait for the creation.
   *
   * @return result of the universe create operation.
   */
  @ApiOperation(value = "Create a YugaByte Universe", response = UniverseResp.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "univ_def",
          value = "univ definition",
          dataType = "com.yugabyte.yw.forms.UniverseDefinitionTaskParams",
          paramType = "body",
          required = true))
  public Result create(UUID customerUUID) {
    // Verify the customer with this universe is present.
    Customer customer = Customer.getOrBadRequest(customerUUID);
    UniverseResp universeResp =
        universeCRUDHandler.createUniverse(
            customer, bindFormDataToTaskParams(request(), UniverseDefinitionTaskParams.class));

    auditService().createAuditEntryWithReqBody(ctx(), universeResp.taskUUID);
    return YWResults.withData(universeResp);
  }

  /**
   * API that queues a task to update/edit a universe of a given customer. This does not wait for
   * the completion.
   *
   * @return result of the universe update operation.
   */
  @ApiOperation(value = "updateUniverse", response = UniverseResp.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "univ_def",
          value = "univ definition",
          dataType = "com.yugabyte.yw.forms.UniverseDefinitionTaskParams",
          paramType = "body",
          required = true))
  public Result update(UUID customerUUID, UUID universeUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getValidUniverseOrBadRequest(universeUUID, customer);
    UniverseDefinitionTaskParams taskParams =
        bindFormDataToTaskParams(request(), UniverseDefinitionTaskParams.class);
    UUID taskUUID = universeCRUDHandler.update(customer, universe, taskParams);
    auditService().createAuditEntryWithReqBody(ctx(), taskUUID);
    return YWResults.withData(
        UniverseResp.create(universe, taskUUID, runtimeConfigFactory.globalRuntimeConf()));
  }

  /** List the universes for a given customer. */
  @ApiOperation(value = "List Universes", response = UniverseResp.class, responseContainer = "List")
  public Result list(UUID customerUUID, String name) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    // Verify the customer is present.
    if (name != null) {
      LOG.info("Finding Universe with name {}.", name);
      return YWResults.withData(universeCRUDHandler.findByName(name));
    }
    return YWResults.withData(universeCRUDHandler.list(customer));
  }

  @ApiOperation(value = "getUniverse", response = UniverseResp.class)
  public Result index(UUID customerUUID, UUID universeUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getValidUniverseOrBadRequest(universeUUID, customer);
    return YWResults.withData(
        UniverseResp.create(universe, null, runtimeConfigFactory.globalRuntimeConf()));
  }

  @ApiOperation(value = "Destroy the universe", response = YWResults.YWTask.class)
  public Result destroy(
      UUID customerUUID, UUID universeUUID, boolean isForceDelete, boolean isDeleteBackups) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getValidUniverseOrBadRequest(universeUUID, customer);

    UUID taskUUID = universeCRUDHandler.destroy(customer, universe, isForceDelete, isDeleteBackups);
    auditService().createAuditEntry(ctx(), request(), taskUUID);
    return new YWResults.YWTask(taskUUID, universe.universeUUID).asResult();
  }

  /**
   * API that queues a task to create a read-only cluster in an existing universe.
   *
   * @return result of the cluster create operation.
   */
  @ApiOperation(value = "clusterCreate", response = UniverseResp.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "univ_def",
          value = "univ definition",
          dataType = "com.yugabyte.yw.forms.UniverseDefinitionTaskParams",
          paramType = "body",
          required = true))
  public Result clusterCreate(UUID customerUUID, UUID universeUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getValidUniverseOrBadRequest(universeUUID, customer);

    UUID taskUUID =
        universeCRUDHandler.createCluster(
            customer,
            universe,
            bindFormDataToTaskParams(request(), UniverseDefinitionTaskParams.class));

    auditService().createAuditEntryWithReqBody(ctx(), taskUUID);
    return YWResults.withData(
        UniverseResp.create(universe, taskUUID, runtimeConfigFactory.globalRuntimeConf()));
  }

  /**
   * API that queues a task to delete a read-only cluster in an existing universe.
   *
   * @return result of the cluster delete operation.
   */
  @ApiOperation(value = "clusterDelete", response = UniverseResp.class)
  public Result clusterDelete(
      UUID customerUUID, UUID universeUUID, UUID clusterUUID, Boolean isForceDelete) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getValidUniverseOrBadRequest(universeUUID, customer);

    UUID taskUUID =
        universeCRUDHandler.clusterDelete(customer, universe, clusterUUID, isForceDelete);

    auditService().createAuditEntry(ctx(), request(), taskUUID);
    return YWResults.withData(
        UniverseResp.create(universe, taskUUID, runtimeConfigFactory.globalRuntimeConf()));
  }

  /**
   * API that queues a task to perform an upgrade and a subsequent rolling restart of a universe.
   *
   * @return result of the universe update operation.
   */
  @ApiOperation(value = "Upgrade  the universe", response = YWResults.YWTask.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "upgrade_params",
          value = "upgrade params",
          dataType = "com.yugabyte.yw.forms.UpgradeParams",
          required = true,
          paramType = "body"))
  public Result upgrade(UUID customerUUID, UUID universeUUID) {
    LOG.info("Upgrade {} for {}.", customerUUID, universeUUID);
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getValidUniverseOrBadRequest(universeUUID, customer);
    UpgradeParams taskParams = bindFormDataToTaskParams(request(), UpgradeParams.class);

    UUID taskUUID = universeCRUDHandler.upgrade(customer, universe, taskParams);
    auditService().createAuditEntryWithReqBody(ctx(), taskUUID);
    return new YWResults.YWTask(taskUUID, universe.universeUUID).asResult();
  }

  @ApiOperation(value = "updateDiskSize", response = YWResults.YWTask.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "univ_def",
          value = "univ definition",
          dataType = "com.yugabyte.yw.forms.DiskIncreaseFormData",
          paramType = "body",
          required = true))
  public Result updateDiskSize(UUID customerUUID, UUID universeUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getValidUniverseOrBadRequest(universeUUID, customer);

    UUID taskUUID =
        universeCRUDHandler.updateDiskSize(
            customer, universe, bindFormDataToTaskParams(request(), DiskIncreaseFormData.class));
    auditService().createAuditEntryWithReqBody(ctx(), taskUUID);
    return new YWResults.YWTask(taskUUID, universe.universeUUID).asResult();
  }
}
