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

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;
import static play.mvc.Http.Status.NOT_FOUND;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.net.HostAndPort;
import com.google.inject.Inject;
import com.yugabyte.yw.cloud.UniverseResourceDetails;
import com.yugabyte.yw.cloud.UniverseResourceDetails.Context;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.metrics.MetricQueryHelper;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.HealthCheck;
import com.yugabyte.yw.models.HealthCheck.Details;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.queries.QueryHelper;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.YBClient;
import play.mvc.Http;

@Slf4j
public class UniverseInfoHandler {
  @Inject private MetricQueryHelper metricQueryHelper;
  @Inject private QueryHelper queryHelper;
  @Inject private RuntimeConfigFactory runtimeConfigFactory;
  @Inject private YBClientService ybService;
  @Inject private NodeUniverseManager nodeUniverseManager;

  public UniverseResourceDetails getUniverseResources(
      Customer customer, UniverseDefinitionTaskParams taskParams) {
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
    UniverseResourceDetails.Context context =
        new Context(runtimeConfigFactory.globalRuntimeConf(), customer, taskParams);
    return UniverseResourceDetails.create(nodesInCluster, taskParams, context);
  }

  public List<UniverseResourceDetails> universeListCost(Customer customer) {
    Set<Universe> universeSet;
    try {
      universeSet = customer.getUniverses();
    } catch (RuntimeException e) {
      throw new PlatformServiceException(
          BAD_REQUEST, "No universe found for customer with ID: " + customer.uuid);
    }
    List<UniverseDefinitionTaskParams> taskParamsList =
        universeSet.stream().map(Universe::getUniverseDetails).collect(Collectors.toList());
    List<UniverseResourceDetails> response = new ArrayList<>(universeSet.size());
    Context context =
        new Context(runtimeConfigFactory.globalRuntimeConf(), customer, taskParamsList);
    for (Universe universe : universeSet) {
      try {
        response.add(UniverseResourceDetails.create(universe.getUniverseDetails(), context));
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
      throw new PlatformServiceException(BAD_REQUEST, e.getMessage());
    }
    if (result.has("error")) throw new PlatformServiceException(BAD_REQUEST, result.get("error"));
    return result;
  }

  public List<Details> healthCheck(UUID universeUUID) {
    List<Details> detailsList = new ArrayList<>();
    try {
      List<HealthCheck> checks = HealthCheck.getAll(universeUUID);
      for (HealthCheck check : checks) {
        detailsList.add(check.detailsJson);
      }
    } catch (RuntimeException e) {
      // TODO(API) dig deeper and find root cause of RuntimeException
      throw new PlatformServiceException(BAD_REQUEST, e.getMessage());
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
      HostAndPort leaderMasterHostAndPort = client.getLeaderMasterHostAndPort();
      if (leaderMasterHostAndPort == null) {
        throw new PlatformServiceException(
            BAD_REQUEST, "Leader master not found for universe " + universe.universeUUID);
      }
      return leaderMasterHostAndPort;
    } catch (RuntimeException e) {
      throw new PlatformServiceException(BAD_REQUEST, e.getMessage());
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
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Universe failed to fetch live queries");
    } catch (Throwable t) {
      log.error("Error retrieving queries for universe", t);
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, t.getMessage());
    }
    return resultNode;
  }

  public JsonNode getSlowQueries(Universe universe, String username, String password) {
    JsonNode resultNode;
    try {
      resultNode = queryHelper.slowQueries(universe, username, password);
    } catch (IllegalArgumentException e) {
      log.error(e.getMessage(), e);
      throw new PlatformServiceException(BAD_REQUEST, e.getMessage());
    } catch (NullPointerException e) {
      log.error("Universe does not have a private IP or DNS", e);
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Universe failed to fetch slow queries");
    } catch (Throwable t) {
      log.error("Error retrieving queries for universe", t);
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, t.getMessage());
    }
    return resultNode;
  }

  public JsonNode resetSlowQueries(Universe universe) {
    try {
      return queryHelper.resetQueries(universe);
    } catch (NullPointerException e) {
      // TODO: Investigate why catch NPE??
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Failed reach node, invalid IP or DNS.");
    } catch (Throwable t) {
      // TODO: Investigate why catch Throwable??
      log.error("Error resetting slow queries for universe", t);
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, t.getMessage());
    }
  }

  public Path downloadNodeLogs(
      Customer customer, Universe universe, NodeDetails node, Path targetFile) {
    ShellResponse response =
        nodeUniverseManager.downloadNodeLogs(node, universe, targetFile.toString());

    if (response.code != 0) {
      throw new PlatformServiceException(Http.Status.INTERNAL_SERVER_ERROR, response.message);
    }
    return targetFile;
  }

  public Path downloadUniverseLogs(Customer customer, Universe universe, Path basePath) {
    List<NodeDetails> nodes = universe.getNodes().stream().collect(Collectors.toList());

    for (NodeDetails node : nodes) {
      String nodeName = node.getNodeName();
      Path nodeTargetFile = Paths.get(basePath.toString() + "/" + nodeName + ".tar.gz");
      log.debug("Node target file {}", nodeTargetFile.toString());

      Path targetFile = downloadNodeLogs(customer, universe, node, nodeTargetFile);
    }
    return basePath;
  }
}
