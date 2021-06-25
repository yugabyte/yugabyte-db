// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.annotations.VisibleForTesting;
import com.google.common.net.HostAndPort;
import com.google.inject.Inject;
import com.yugabyte.yw.cloud.UniverseResourceDetails;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.ValidatingFormFactory;
import com.yugabyte.yw.common.YWServiceException;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.forms.*;
import com.yugabyte.yw.forms.YWResults.YWSuccess;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import io.swagger.annotations.*;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.data.Form;
import play.libs.Json;
import play.libs.concurrent.HttpExecutionContext;
import play.mvc.Result;
import play.mvc.Results;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.InputStream;
import java.util.Collections;
import java.util.List;
import java.util.Optional;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;

import static com.yugabyte.yw.controllers.UniverseControllerRequestBinder.bindFormDataToTaskParams;
import static com.yugabyte.yw.forms.YWResults.YWSuccess.empty;
import static com.yugabyte.yw.forms.YWResults.YWSuccess.withMessage;

@Api(value = "Universe", authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class UniverseController extends AuthenticatedController {
  private static final Logger LOG = LoggerFactory.getLogger(UniverseController.class);

  @Inject private UniverseActionsHandler universeActionsHandler;

  @Inject private UniverseCRUDHandler universeCRUDHandler;

  @Inject UniverseInfoHandler universeInfoHandler;

  @Inject UniverseYbDbAdminHandler universeYbDbAdminHandler;

  @Inject ValidatingFormFactory formFactory;

  @Inject Commissioner commissioner;

  @Inject HttpExecutionContext ec;

  @Inject play.Configuration appConfig;

  @Inject private NodeUniverseManager nodeUniverseManager;

  @Inject private RuntimeConfigFactory runtimeConfigFactory;

  @Inject
  public UniverseController() {}

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

  @ApiOperation(value = "setDatabaseCredentials", response = YWSuccess.class)
  public Result setDatabaseCredentials(UUID customerUUID, UUID universeUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getValidUniverseOrBadRequest(universeUUID, customer);

    universeYbDbAdminHandler.setDatabaseCredentials(
        customer,
        universe,
        formFactory.getFormDataOrBadRequest(DatabaseSecurityFormData.class).get());

    // TODO: Missing Audit
    return withMessage("Updated security in DB.");
  }

  @ApiOperation(value = "createUserInDB", response = YWSuccess.class)
  public Result createUserInDB(UUID customerUUID, UUID universeUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getValidUniverseOrBadRequest(universeUUID, customer);

    DatabaseUserFormData data =
        formFactory.getFormDataOrBadRequest(DatabaseUserFormData.class).get();

    universeYbDbAdminHandler.createUserInDB(customer, universe, data);

    // TODO: Missing Audit
    return withMessage("Created user in DB.");
  }

  @VisibleForTesting static final String DEPRECATED = "Deprecated.";

  @ApiOperation(
      value = "run command in shell",
      notes = "This operation is no longer supported due to security reasons",
      response = YWResults.YWError.class)
  public Result runInShell(UUID customerUUID, UUID universeUUID) {
    throw new YWServiceException(BAD_REQUEST, DEPRECATED);
  }

  @ApiOperation(
      value = "Run YSQL query against this universe",
      notes = "Only valid when platform is running in mode is `OSS`",
      response = Object.class)
  public Result runQuery(UUID customerUUID, UUID universeUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getValidUniverseOrBadRequest(universeUUID, customer);
    Form<RunQueryFormData> formData = formFactory.getFormDataOrBadRequest(RunQueryFormData.class);

    JsonNode queryResult =
        universeYbDbAdminHandler.validateRequestAndExecuteQuery(universe, formData.get());
    auditService().createAuditEntry(ctx(), request(), Json.toJson(formData.data()));
    return YWResults.withRawData(queryResult);
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

  // TODO: This method take params in post body instead of looking up the params of universe in db
  //  this needs to be fixed as follows:
  // 1> There should be a GET method to read universe resources without giving the params in the
  // body
  // 2> Map this to HTTP GET request.
  // 3> In addition /universe_configure should also return resource estimation info back.
  @ApiOperation(
      value = "Api to get the resource estimate for a universe",
      notes =
          "Expects UniverseDefinitionTaskParams in request body and calculates the resource "
              + "estimate for NodeDetailsSet in that.",
      response = UniverseResourceDetails.class)
  public Result getUniverseResources(UUID customerUUID) {
    UniverseDefinitionTaskParams taskParams =
        bindFormDataToTaskParams(request(), UniverseDefinitionTaskParams.class);
    return YWResults.withData(universeInfoHandler.getUniverseResources(taskParams));
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

  @ApiOperation(value = "setUniverseKey", response = UniverseResp.class)
  public Result setUniverseKey(UUID customerUUID, UUID universeUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getValidUniverseOrBadRequest(universeUUID, customer);

    LOG.info("Updating universe key {} for {}.", universe.universeUUID, customer.uuid);
    // Get the user submitted form data.

    EncryptionAtRestKeyParams taskParams =
        EncryptionAtRestKeyParams.bindFromFormData(universe.universeUUID, request());

    UUID taskUUID = universeActionsHandler.setUniverseKey(customer, universe, taskParams);

    auditService().createAuditEntryWithReqBody(ctx(), taskUUID);
    UniverseResp resp =
        UniverseResp.create(universe, taskUUID, runtimeConfigFactory.globalRuntimeConf());
    return YWResults.withData(resp);
  }

  /**
   * API that sets universe version number to -1
   *
   * @return result of settings universe version to -1 (either success if universe exists else
   *     failure
   */
  @ApiOperation(value = "resetVersion", response = YWSuccess.class)
  public Result resetVersion(UUID customerUUID, UUID universeUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getValidUniverseOrBadRequest(universeUUID, customer);

    universe.resetVersion();
    return empty();
  }

  /**
   * API that downloads the log files for a particular node in a universe. Synchronized due to
   * potential race conditions.
   *
   * @param customerUUID ID of custoemr
   * @param universeUUID ID of universe
   * @param nodeName name of the node
   * @return tar file of the tserver and master log files (if the node is a master server).
   */
  // TODO: API
  public CompletionStage<Result> downloadNodeLogs(
      UUID customerUUID, UUID universeUUID, String nodeName) {
    return CompletableFuture.supplyAsync(
        () -> {
          Customer customer = Customer.getOrBadRequest(customerUUID);
          Universe universe = Universe.getValidUniverseOrBadRequest(universeUUID, customer);
          NodeDetails node;
          String storagePath, tarFileName, targetFile;
          ShellResponse response;

          LOG.debug("Retrieving logs for " + nodeName);
          node = universe.getNode(nodeName);
          storagePath = runtimeConfigFactory.staticApplicationConf().getString("yb.storage.path");
          tarFileName = node.cloudInfo.private_ip + "-logs.tar.gz";
          targetFile = storagePath + "/" + tarFileName;
          response = nodeUniverseManager.downloadNodeLogs(node, universe, targetFile);

          if (response.code != 0) {
            throw new YWServiceException(INTERNAL_SERVER_ERROR, response.message);
          }

          try {
            File file = new File(targetFile);
            InputStream is = new FileInputStream(file);
            file.delete();
            // return file to client
            response().setHeader("Content-Disposition", "attachment; filename=" + tarFileName);
            return ok(is).as("application/x-compressed");
          } catch (FileNotFoundException e) {
            throw new YWServiceException(INTERNAL_SERVER_ERROR, response.message);
          }
        },
        ec.current());
  }

  /**
   * API that toggles TLS state of the universe. Can enable/disable node to node and client to node
   * encryption. Supports rolling and non-rolling upgrade of the universe.
   *
   * @param customerUuid ID of customer
   * @param universeUuid ID of universe
   * @return Result of update operation with task id
   */
  public Result toggleTls(UUID customerUuid, UUID universeUuid) {
    Customer customer = Customer.getOrBadRequest(customerUuid);
    Universe universe = Universe.getValidUniverseOrBadRequest(universeUuid, customer);
    ObjectNode formData = (ObjectNode) request().body().asJson();
    ToggleTlsParams requestParams = ToggleTlsParams.bindFromFormData(formData);

    UUID taskUUID = universeActionsHandler.toggleTls(customer, universe, requestParams);
    auditService().createAuditEntry(ctx(), request(), Json.toJson(formData), taskUUID);
    return YWResults.withData(
        UniverseResp.create(universe, taskUUID, runtimeConfigFactory.globalRuntimeConf()));
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

  /**
   * Mark whether the universe needs to be backed up or not.
   *
   * @return Result
   */
  @ApiOperation(value = "Set backup Flag for a universe", response = YWSuccess.class)
  public Result setBackupFlag(UUID customerUUID, UUID universeUUID, Boolean markActive) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getValidUniverseOrBadRequest(universeUUID, customer);

    universeActionsHandler.setBackupFlag(universe, markActive);
    auditService().createAuditEntry(ctx(), request());
    return empty();
  }

  /**
   * Mark whether the universe has been made helm compatible.
   *
   * @return Result
   */
  @ApiOperation(value = "Set the universe as helm3 compatible", response = YWSuccess.class)
  public Result setHelm3Compatible(UUID customerUUID, UUID universeUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getValidUniverseOrBadRequest(universeUUID, customer);
    universeActionsHandler.setHelm3Compatible(universe);
    auditService().createAuditEntry(ctx(), request());
    return empty();
  }

  @ApiOperation(value = "Configure Alerts for a universe", response = YWSuccess.class)
  public Result configureAlerts(UUID customerUUID, UUID universeUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getValidUniverseOrBadRequest(universeUUID, customer);
    universeActionsHandler.configureAlerts(
        universe, formFactory.getFormDataOrBadRequest(AlertConfigFormData.class));
    // TODO Audit ??
    return empty();
  }

  @ApiOperation(value = "getUniverse", response = UniverseResp.class)
  public Result index(UUID customerUUID, UUID universeUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getValidUniverseOrBadRequest(universeUUID, customer);
    return YWResults.withData(
        UniverseResp.create(universe, null, runtimeConfigFactory.globalRuntimeConf()));
  }

  @ApiOperation(value = "Pause the universe", response = YWResults.YWTask.class)
  public Result pause(UUID customerUUID, UUID universeUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getValidUniverseOrBadRequest(universeUUID, customer);

    UUID taskUUID = universeActionsHandler.pause(customer, universe);
    auditService().createAuditEntry(ctx(), request(), taskUUID);
    return new YWResults.YWTask(taskUUID, universe.universeUUID).asResult();
  }

  @ApiOperation(value = "Resume the universe", response = YWResults.YWTask.class)
  public Result resume(UUID customerUUID, UUID universeUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getValidUniverseOrBadRequest(universeUUID, customer);

    UUID taskUUID = universeActionsHandler.resume(customer, universe);

    auditService().createAuditEntry(ctx(), request(), taskUUID);
    return new YWResults.YWTask(taskUUID, universe.universeUUID).asResult();
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

  @ApiOperation(value = "universeCost", response = UniverseResourceDetails.class)
  public Result universeCost(UUID customerUUID, UUID universeUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getValidUniverseOrBadRequest(universeUUID, customer);

    return YWResults.withData(
        UniverseResourceDetails.create(
            universe.getUniverseDetails(), runtimeConfigFactory.globalRuntimeConf()));
  }

  @ApiOperation(
      value = "list universe cost for all universes",
      response = UniverseResourceDetails.class)
  public Result universeListCost(UUID customerUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    return YWResults.withData(universeInfoHandler.universeListCost(customer));
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

  /**
   * API that checks the status of the the tservers and masters in the universe.
   *
   * @return result of the universe status operation.
   */
  @ApiOperation(
      value = "Status of the Universe",
      notes = "This will return a Map of node name to its status in json format",
      responseContainer = "Map",
      response = Object.class)
  // TODO document error case.
  public Result status1(UUID customerUUID, UUID universeUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getValidUniverseOrBadRequest(universeUUID, customer);

    // Get alive status
    JsonNode result = universeInfoHandler.status(universe);
    return Results.status(OK, result);
  }

  /**
   * API that checks the health of all the tservers and masters in the universe, as well as certain
   * conditions on the machines themselves, such as disk utilization, presence of FATAL or core
   * files, etc.
   *
   * @return result of the checker script
   */
  @ApiOperation(value = "health Check", response = Object.class)
  public Result healthCheck(UUID customerUUID, UUID universeUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe.getValidUniverseOrBadRequest(universeUUID, customer);

    List<String> detailsList = universeInfoHandler.healthCheck(universeUUID);
    return YWResults.withData(detailsList);
  }

  /**
   * Endpoint to retrieve the IP of the master leader for a given universe.
   *
   * @param customerUUID UUID of Customer the target Universe belongs to.
   * @param universeUUID UUID of Universe to retrieve the master leader private IP of.
   * @return The private IP of the master leader.
   */
  @ApiOperation(value = "getMasterLeaderIP", response = Object.class)
  public Result getMasterLeaderIP(UUID customerUUID, UUID universeUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getValidUniverseOrBadRequest(universeUUID, customer);

    HostAndPort leaderMasterHostAndPort = universeInfoHandler.getMasterLeaderIP(universe);
    ObjectNode result = Json.newObject().put("privateIP", leaderMasterHostAndPort.getHost());
    return YWResults.withRawData(result);
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

  @ApiOperation(value = "getLiveQueries", response = Object.class)
  public Result getLiveQueries(UUID customerUUID, UUID universeUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getValidUniverseOrBadRequest(universeUUID, customer);
    LOG.info("Live queries for customer {}, universe {}", customer.uuid, universe.universeUUID);
    JsonNode resultNode = universeInfoHandler.getLiveQuery(universe);
    return Results.ok(resultNode);
  }

  @ApiOperation(value = "getSlowQueries", response = Object.class)
  public Result getSlowQueries(UUID customerUUID, UUID universeUUID) {
    LOG.info("Slow queries for customer {}, universe {}", customerUUID, universeUUID);
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getValidUniverseOrBadRequest(universeUUID, customer);

    JsonNode resultNode = universeInfoHandler.getSlowQueries(universe);
    return Results.ok(resultNode);
  }

  public Result resetSlowQueries(UUID customerUUID, UUID universeUUID) {
    LOG.info("Resetting Slow queries for customer {}, universe {}", customerUUID, universeUUID);
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getValidUniverseOrBadRequest(universeUUID, customer);

    return YWResults.withRawData(universeInfoHandler.resetSlowQueries(universe));
  }
}
