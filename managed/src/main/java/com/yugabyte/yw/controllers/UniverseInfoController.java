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
import com.google.common.collect.ImmutableSet;
import com.google.common.net.HostAndPort;
import com.google.inject.Inject;
import com.yugabyte.yw.cloud.UniverseResourceDetails;
import com.yugabyte.yw.cloud.UniverseResourceDetails.Context;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.AppConfigHelper;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.UniverseInterruptionResult;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.utils.FileUtils;
import com.yugabyte.yw.controllers.handlers.UniverseInfoHandler;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.TriggerHealthCheckResult;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.HealthCheck.Details;
import com.yugabyte.yw.models.HealthCheck.Details.NodeData;
import com.yugabyte.yw.models.MasterInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.extended.DetailsExt;
import com.yugabyte.yw.models.extended.NodeDataExt;
import com.yugabyte.yw.models.helpers.NodeDetails;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import java.io.File;
import java.io.InputStream;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.time.OffsetDateTime;
import java.time.ZoneOffset;
import java.util.Date;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionStage;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import play.libs.Json;
import play.libs.concurrent.HttpExecutionContext;
import play.mvc.Http;
import play.mvc.Result;
import play.mvc.Results;

@Api(
    value = "Universe information",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
@Slf4j
public class UniverseInfoController extends AuthenticatedController {

  @Inject private RuntimeConfigFactory runtimeConfigFactory;
  @Inject private RuntimeConfGetter confGetter;
  @Inject private UniverseInfoHandler universeInfoHandler;
  @Inject private HttpExecutionContext ec;

  private static final String YSQL_USERNAME_HEADER = "ysql-username";
  private static final String YSQL_PASSWORD_HEADER = "ysql-password";

  /**
   * API that checks the status of the the tservers and masters in the universe.
   *
   * @return result of the universe status operation.
   */
  @ApiOperation(
      value = "Get a universe's status",
      notes = "This will return a Map of node name to its status in json format",
      responseContainer = "Map",
      response = Object.class)
  // TODO API document error case.
  public Result universeStatus(UUID customerUUID, UUID universeUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID, customer);

    // Get alive status
    JsonNode result = universeInfoHandler.status(universe);
    return PlatformResults.withRawData(result);
  }

  @ApiOperation(
      value = "Get a universe's spot instances' status",
      hidden = true,
      notes = "This will return a Map of node name to its interruption status in json format",
      response = UniverseInterruptionResult.class)
  public Result spotInstanceStatus(UUID customerUUID, UUID universeUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID, customer);

    Set<CloudType> validClouds = ImmutableSet.of(CloudType.aws, CloudType.azu, CloudType.gcp);
    UserIntent userIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;

    if (!userIntent.useSpotInstance || !validClouds.contains(userIntent.providerType)) {
      throw new PlatformServiceException(BAD_REQUEST, "The universe doesn't use spot instances.");
    }

    UniverseInterruptionResult result = universeInfoHandler.spotUniverseStatus(universe);
    return PlatformResults.withData(result);
  }

  @ApiOperation(
      value = "Get a resource usage estimate for a universe",
      hidden = true,
      notes =
          "Expects UniverseDefinitionTaskParams in request body and calculates the resource "
              + "estimate for NodeDetailsSet in that.",
      response = UniverseResourceDetails.class)
  public Result getUniverseResources(UUID customerUUID, UUID universeUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID, customer);
    return PlatformResults.withData(
        universeInfoHandler.getUniverseResources(customer, universe.getUniverseDetails()));
  }

  @ApiOperation(
      value = "Get a cost estimate for a universe",
      nickname = "getUniverseCost",
      response = UniverseResourceDetails.class)
  public Result universeCost(UUID customerUUID, UUID universeUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID, customer);

    Context context =
        new Context(
            runtimeConfigFactory.globalRuntimeConf(), customer, universe.getUniverseDetails());
    return PlatformResults.withData(
        UniverseResourceDetails.create(universe.getUniverseDetails(), context));
  }

  @ApiOperation(
      value = "Get a cost estimate for all universes",
      nickname = "getUniverseCostForAll",
      responseContainer = "List",
      response = UniverseResourceDetails.class)
  public Result universeListCost(UUID customerUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    return PlatformResults.withData(universeInfoHandler.universeListCost(customer));
  }

  /**
   * Endpoint to retrieve the IP of the master leader for a given universe.
   *
   * @param customerUUID UUID of Customer the target Universe belongs to.
   * @param universeUUID UUID of Universe to retrieve the master leader private IP of.
   * @return The private IP of the master leader.
   */
  @ApiOperation(
      value = "Get IP address of a universe's master leader",
      nickname = "getMasterLeaderIP",
      response = Object.class)
  public Result getMasterLeaderIP(UUID customerUUID, UUID universeUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID, customer);

    HostAndPort leaderMasterHostAndPort = universeInfoHandler.getMasterLeaderIP(universe);
    ObjectNode result = Json.newObject().put("privateIP", leaderMasterHostAndPort.getHost());
    return PlatformResults.withRawData(result);
  }

  @ApiOperation(
      value = "Get live queries for a universe",
      nickname = "getLiveQueries",
      response = Object.class)
  public Result getLiveQueries(UUID customerUUID, UUID universeUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID, customer);
    if (universe.getUniverseDetails().universePaused) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Can't get live queries for a paused universe");
    }
    log.info(
        "Live queries for customer {}, universe {}",
        customer.getUuid(),
        universe.getUniverseUUID());
    JsonNode resultNode = universeInfoHandler.getLiveQuery(universe);
    return PlatformResults.withRawData(resultNode);
  }

  @ApiOperation(
      value = "Get slow queries for a universe",
      nickname = "getSlowQueries",
      response = Object.class)
  public Result getSlowQueries(UUID customerUUID, UUID universeUUID) {
    log.info("Slow queries for customer {}, universe {}", customerUUID, universeUUID);
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID, customer);
    if (universe.getUniverseDetails().universePaused) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Can't get slow queries for a paused universe");
    }
    JsonNode resultNode = universeInfoHandler.getSlowQueries(universe);
    return Results.ok(resultNode);
  }

  @ApiOperation(
      value = "Reset slow queries for a universe",
      nickname = "resetSlowQueries",
      response = Object.class)
  public Result resetSlowQueries(UUID customerUUID, UUID universeUUID, Http.Request request) {
    log.info("Resetting Slow queries for customer {}, universe {}", customerUUID, universeUUID);
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID, customer);
    if (universe.getUniverseDetails().universePaused) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Can't reset slow queries for a paused universe");
    }
    auditService()
        .createAuditEntry(
            request,
            Audit.TargetType.Universe,
            universeUUID.toString(),
            Audit.ActionType.ResetSlowQueries);
    return PlatformResults.withRawData(universeInfoHandler.resetSlowQueries(universe));
  }

  /**
   * API that checks the health of all the tservers and masters in the universe, as well as certain
   * conditions on the machines themselves, such as disk utilization, presence of FATAL or core
   * files, etc.
   *
   * @return result of the checker script
   */
  @ApiOperation(
      value = "Run a universe health check",
      notes =
          "Checks the health of all tablet servers and masters in the universe, as well as certain conditions on the machines themselves, including disk utilization, presence of FATAL or core files, and more.",
      nickname = "healthCheckUniverse",
      responseContainer = "List",
      response = Details.class)
  public Result healthCheck(UUID customerUUID, UUID universeUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe.getOrBadRequest(universeUUID, customer);

    List<Details> detailsList = universeInfoHandler.healthCheck(universeUUID);
    return PlatformResults.withData(convertDetails(detailsList));
  }

  @ApiOperation(
      value = "Trigger a universe health check",
      notes = "Trigger a universe health check and return the trigger time.",
      response = TriggerHealthCheckResult.class)
  public Result triggerHealthCheck(UUID customerUUID, UUID universeUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID, customer);

    if (!confGetter.getConfForScope(universe, UniverseConfKeys.enableTriggerAPI)) {
      throw new PlatformServiceException(
          METHOD_NOT_ALLOWED, "Manual health check trigger is disabled.");
    }

    OffsetDateTime dt = OffsetDateTime.now(ZoneOffset.UTC);
    universeInfoHandler.triggerHealthCheck(customer, universe);

    TriggerHealthCheckResult res = new TriggerHealthCheckResult();
    res.timestamp = new Date(dt.toInstant().toEpochMilli());

    return PlatformResults.withData(res);
  }

  /**
   * API that downloads the log files for a particular node in a universe. Synchronized due to
   * potential race conditions.
   *
   * @param customerUUID ID of customer
   * @param universeUUID ID of universe
   * @param nodeName name of the node
   * @return tar file of the tserver and master log files (if the node is a master server).
   */
  @ApiOperation(
      value = "Download a node's logs",
      notes = "Downloads the log files from a given node.",
      nickname = "downloadNodeLogs",
      response = String.class,
      produces = "application/x-compressed")
  public CompletionStage<Result> downloadNodeLogs(
      UUID customerUUID, UUID universeUUID, String nodeName) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID, customer);
    log.debug("Retrieving logs for " + nodeName);
    NodeDetails node =
        universe
            .maybeGetNode(nodeName)
            .orElseThrow(() -> new PlatformServiceException(NOT_FOUND, nodeName));
    return CompletableFuture.supplyAsync(
        () -> {
          String storagePath = AppConfigHelper.getStoragePath();
          String tarFileName = node.cloudInfo.private_ip + "-logs.tar.gz";
          Path targetFile = Paths.get(storagePath + "/" + tarFileName);
          File file =
              universeInfoHandler.downloadNodeLogs(customer, universe, node, targetFile).toFile();
          InputStream is = FileUtils.getInputStreamOrFail(file, true /* deleteOnClose */);
          return ok(is)
              .as("application/x-compressed")
              .withHeader("Content-Disposition", "attachment; filename=" + file.getName());
        },
        ec.current());
  }

  @ApiOperation(
      value = "Get master information list",
      nickname = "getMasterInfos",
      response = MasterInfo.class,
      responseContainer = "List")
  public Result getMasterInfos(UUID customerUUID, UUID universeUUID) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID, customer);

    List<MasterInfo> masterInfos = universeInfoHandler.getMasterInfos(universe);
    return PlatformResults.withData(masterInfos);
  }

  private List<DetailsExt> convertDetails(List<Details> details) {
    boolean backwardCompatibleDate =
        confGetter.getGlobalConf(GlobalConfKeys.backwardCompatibleDate);
    return convertDetails(details, backwardCompatibleDate);
  }

  private List<DetailsExt> convertDetails(List<Details> details, boolean backwardCompatibleDate) {
    return details.stream()
        .map(
            d ->
                new DetailsExt()
                    .setDetails(d)
                    .setTimestamp(backwardCompatibleDate ? d.getTimestampIso() : null)
                    .setData(convertNodeData(d.getData(), backwardCompatibleDate)))
        .collect(Collectors.toList());
  }

  private List<NodeDataExt> convertNodeData(
      List<NodeData> nodeDataList, boolean backwardCompatibleDate) {
    return nodeDataList.stream()
        .map(
            data ->
                new NodeDataExt()
                    .setNodeData(data)
                    .setTimestamp(backwardCompatibleDate ? data.getTimestampIso() : null))
        .collect(Collectors.toList());
  }
}
