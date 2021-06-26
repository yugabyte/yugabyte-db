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

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.net.HostAndPort;
import com.google.inject.Inject;
import com.yugabyte.yw.cloud.UniverseResourceDetails;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.YWServiceException;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.metrics.MetricQueryHelper;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.HealthCheck;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.queries.QueryHelper;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.YBClient;
import play.libs.Json;
import play.mvc.Http;

import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

@Slf4j
public class UniverseInfoHandler {
  @Inject private MetricQueryHelper metricQueryHelper;
  @Inject private QueryHelper queryHelper;
  @Inject private RuntimeConfigFactory runtimeConfigFactory;
  @Inject private YBClientService ybService;
  @Inject private NodeUniverseManager nodeUniverseManager;

  public UniverseResourceDetails getUniverseResources(UniverseDefinitionTaskParams taskParams) {
    Set<NodeDetails> nodesInCluster;
    if (taskParams
        .getCurrentClusterType()
        .equals(UniverseDefinitionTaskParams.ClusterType.PRIMARY)) {
      nodesInCluster =
          taskParams
              .nodeDetailsSet
              .stream()
              .filter(n -> n.isInPlacement(taskParams.getPrimaryCluster().uuid))
              .collect(Collectors.toSet());
    } else {
      nodesInCluster =
          taskParams
              .nodeDetailsSet
              .stream()
              .filter(n -> n.isInPlacement(taskParams.getReadOnlyClusters().get(0).uuid))
              .collect(Collectors.toSet());
    }
    return UniverseResourceDetails.create(
        nodesInCluster, taskParams, runtimeConfigFactory.globalRuntimeConf());
  }

  public List<UniverseResourceDetails> universeListCost(Customer customer) {
    Set<Universe> universeSet;
    try {
      universeSet = customer.getUniverses();
    } catch (RuntimeException e) {
      throw new YWServiceException(
          BAD_REQUEST, "No universe found for customer with ID: " + customer.uuid);
    }
    List<UniverseResourceDetails> response = new ArrayList<>(universeSet.size());
    for (Universe universe : universeSet) {
      try {
        response.add(
            UniverseResourceDetails.create(
                universe.getUniverseDetails(), runtimeConfigFactory.globalRuntimeConf()));
      } catch (Exception e) {
        log.error("Could not add cost details for Universe with UUID: " + universe.universeUUID);
      }
    }
    return response;
  }

  public JsonNode status(Universe universe) {
    JsonNode result;
    try {
      result = PlacementInfoUtil.getUniverseAliveStatus(universe, metricQueryHelper);
    } catch (RuntimeException e) {
      // TODO(API) dig deeper and find root cause of RuntimeException
      throw new YWServiceException(BAD_REQUEST, e.getMessage());
    }
    if (result.has("error")) throw new YWServiceException(BAD_REQUEST, result.get("error"));
    return result;
  }

  public List<String> healthCheck(UUID universeUUID) {
    List<String> detailsList = new ArrayList<>();
    try {
      List<HealthCheck> checks = HealthCheck.getAll(universeUUID);
      for (HealthCheck check : checks) {
        detailsList.add(Json.stringify(Json.parse(check.detailsJson)));
      }
    } catch (RuntimeException e) {
      // TODO(API) dig deeper and find root cause of RuntimeException
      throw new YWServiceException(BAD_REQUEST, e.getMessage());
    }
    return detailsList;
  }

  public HostAndPort getMasterLeaderIP(Universe universe) {
    final String hostPorts = universe.getMasterAddresses();
    String certificate = universe.getCertificateNodetoNode();
    YBClient client = null;
    // Get and return Leader IP
    try {
      client = ybService.getClient(hostPorts, certificate);
      return client.getLeaderMasterHostAndPort();
    } catch (RuntimeException e) {
      throw new YWServiceException(BAD_REQUEST, e.getMessage());
    } finally {
      ybService.closeClient(client, hostPorts);
    }
  }

  public JsonNode getLiveQuery(Universe universe) {
    JsonNode resultNode;
    try {
      resultNode = queryHelper.liveQueries(universe);
    } catch (NullPointerException e) {
      log.error("Universe does not have a private IP or DNS", e);
      throw new YWServiceException(INTERNAL_SERVER_ERROR, "Universe failed to fetch live queries");
    } catch (Throwable t) {
      log.error("Error retrieving queries for universe", t);
      throw new YWServiceException(INTERNAL_SERVER_ERROR, t.getMessage());
    }
    return resultNode;
  }

  public JsonNode getSlowQueries(Universe universe) {
    JsonNode resultNode;
    try {
      resultNode = queryHelper.slowQueries(universe);
    } catch (NullPointerException e) {
      log.error("Universe does not have a private IP or DNS", e);
      throw new YWServiceException(INTERNAL_SERVER_ERROR, "Universe failed to fetch slow queries");
    } catch (Throwable t) {
      log.error("Error retrieving queries for universe", t);
      throw new YWServiceException(INTERNAL_SERVER_ERROR, t.getMessage());
    }
    return resultNode;
  }

  public JsonNode resetSlowQueries(Universe universe) {
    try {
      return queryHelper.resetQueries(universe);
    } catch (NullPointerException e) {
      // TODO: Investigate why catch NPE??
      throw new YWServiceException(INTERNAL_SERVER_ERROR, "Failed reach node, invalid IP or DNS.");
    } catch (Throwable t) {
      // TODO: Investigate why catch Throwable??
      log.error("Error resetting slow queries for universe", t);
      throw new YWServiceException(INTERNAL_SERVER_ERROR, t.getMessage());
    }
  }

  public Path downloadNodeLogs(UUID customerUUID, UUID universeUUID, String nodeName) {
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getValidUniverseOrBadRequest(universeUUID, customer);
    log.debug("Retrieving logs for " + nodeName);
    NodeDetails node = universe.getNode(nodeName);
    String storagePath = runtimeConfigFactory.staticApplicationConf().getString("yb.storage.path");
    String tarFileName = node.cloudInfo.private_ip + "-logs.tar.gz";
    Path targetFile = Paths.get(storagePath + "/" + tarFileName);
    ShellResponse response =
        nodeUniverseManager.downloadNodeLogs(node, universe, targetFile.toString());

    if (response.code != 0) {
      throw new YWServiceException(Http.Status.INTERNAL_SERVER_ERROR, response.message);
    }
    return targetFile;
  }
}
