/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.controllers;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.net.HostAndPort;
import com.google.inject.Inject;
import com.yugabyte.yw.cloud.UniverseResourceDetails;
import com.yugabyte.yw.common.YWServiceException;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.controllers.handlers.UniverseInfoHandler;
import com.yugabyte.yw.forms.YWResults;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.HealthCheck.Details;
import com.yugabyte.yw.models.Universe;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.InputStream;
import java.util.List;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import lombok.extern.slf4j.Slf4j;
import play.libs.Json;
import play.libs.concurrent.HttpExecutionContext;
import play.mvc.Result;
import play.mvc.Results;

@Api(
    value = "UniverseInfo",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
@Slf4j
public class UniverseInfoController extends AuthenticatedController {

  @Inject private RuntimeConfigFactory runtimeConfigFactory;
  @Inject private UniverseInfoHandler universeInfoHandler;
  @Inject private HttpExecutionContext ec;

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
  // TODO API document error case.
  public Result universeStatus(UUID customerUUID, UUID universeUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getValidUniverseOrBadRequest(universeUUID, customer);

    // Get alive status
    JsonNode result = universeInfoHandler.status(universe);
    return YWResults.withRawData(result);
  }

  @ApiOperation(
      value = "Api to get the resource estimate for a universe",
      hidden = true,
      notes =
          "Expects UniverseDefinitionTaskParams in request body and calculates the resource "
              + "estimate for NodeDetailsSet in that.",
      response = UniverseResourceDetails.class)
  public Result getUniverseResources(UUID customerUUID, UUID universeUUID) {
    Universe universe = Universe.getOrBadRequest(universeUUID);
    return YWResults.withData(
        universeInfoHandler.getUniverseResources(universe.getUniverseDetails()));
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
      responseContainer = "List",
      response = UniverseResourceDetails.class)
  public Result universeListCost(UUID customerUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    return YWResults.withData(universeInfoHandler.universeListCost(customer));
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

  @ApiOperation(value = "getLiveQueries", response = Object.class)
  public Result getLiveQueries(UUID customerUUID, UUID universeUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getValidUniverseOrBadRequest(universeUUID, customer);
    log.info("Live queries for customer {}, universe {}", customer.uuid, universe.universeUUID);
    JsonNode resultNode = universeInfoHandler.getLiveQuery(universe);
    return YWResults.withRawData(resultNode);
  }

  @ApiOperation(value = "getSlowQueries", response = Object.class)
  public Result getSlowQueries(UUID customerUUID, UUID universeUUID) {
    log.info("Slow queries for customer {}, universe {}", customerUUID, universeUUID);
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getValidUniverseOrBadRequest(universeUUID, customer);

    JsonNode resultNode = universeInfoHandler.getSlowQueries(universe);
    return Results.ok(resultNode);
  }

  @ApiOperation(value = "resetSlowQueries", response = Object.class)
  public Result resetSlowQueries(UUID customerUUID, UUID universeUUID) {
    log.info("Resetting Slow queries for customer {}, universe {}", customerUUID, universeUUID);
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getValidUniverseOrBadRequest(universeUUID, customer);

    return YWResults.withRawData(universeInfoHandler.resetSlowQueries(universe));
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

    List<Details> detailsList = universeInfoHandler.healthCheck(universeUUID);
    return YWResults.withData(detailsList);
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
  @ApiOperation(value = "download Node logs", produces = "application/x-compressed")
  public CompletionStage<Result> downloadNodeLogs(
      UUID customerUUID, UUID universeUUID, String nodeName) {
    return CompletableFuture.supplyAsync(
        () -> {
          File file =
              universeInfoHandler.downloadNodeLogs(customerUUID, universeUUID, nodeName).toFile();
          InputStream is = getInputStreamOrFail(file);
          file.delete(); // TODO: should this be done in finally?
          // return the file to client
          response().setHeader("Content-Disposition", "attachment; filename=" + file.getName());
          return ok(is).as("application/x-compressed");
        },
        ec.current());
  }

  private static InputStream getInputStreamOrFail(File file) {
    try {
      return new FileInputStream(file);
    } catch (FileNotFoundException e) {
      throw new YWServiceException(INTERNAL_SERVER_ERROR, e.getMessage());
    }
  }
}
