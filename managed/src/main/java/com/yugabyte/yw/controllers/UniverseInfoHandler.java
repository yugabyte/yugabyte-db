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
import com.google.common.net.HostAndPort;
import com.google.inject.Inject;
import com.yugabyte.yw.cloud.UniverseResourceDetails;
import com.yugabyte.yw.common.PlacementInfoUtil;
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
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.yb.client.YBClient;
import play.libs.Json;

import java.util.ArrayList;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

public class UniverseInfoHandler {
  private static final Logger LOG = LoggerFactory.getLogger(UniverseInfoHandler.class);
  @Inject private MetricQueryHelper metricQueryHelper;
  @Inject private QueryHelper queryHelper;
  @Inject private RuntimeConfigFactory runtimeConfigFactory;
  @Inject private YBClientService ybService;

  public UniverseInfoHandler() {}

  UniverseResourceDetails getUniverseResources(UniverseDefinitionTaskParams taskParams) {
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

  List<UniverseResourceDetails> universeListCost(Customer customer) {
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
        LOG.error("Could not add cost details for Universe with UUID: " + universe.universeUUID);
      }
    }
    return response;
  }

  JsonNode status(Universe universe) {
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

  List<String> healthCheck(UUID universeUUID) {
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

  HostAndPort getMasterLeaderIP(Universe universe) {
    final String hostPorts = universe.getMasterAddresses();
    String certificate = universe.getCertificateNodetoNode();
    YBClient client = null;
    // Get and return Leader IP
    HostAndPort leaderMasterHostAndPort = null;
    try {
      client = ybService.getClient(hostPorts, certificate);
      leaderMasterHostAndPort = client.getLeaderMasterHostAndPort();
    } catch (RuntimeException e) {
      throw new YWServiceException(BAD_REQUEST, e.getMessage());
    } finally {
      ybService.closeClient(client, hostPorts);
    }
    return leaderMasterHostAndPort;
  }

  JsonNode getLiveQuery(Universe universe) {
    JsonNode resultNode;
    try {
      resultNode = queryHelper.liveQueries(universe);
    } catch (NullPointerException e) {
      LOG.error("Universe does not have a private IP or DNS", e);
      throw new YWServiceException(INTERNAL_SERVER_ERROR, "Universe failed to fetch live queries");
    } catch (Throwable t) {
      LOG.error("Error retrieving queries for universe", t);
      throw new YWServiceException(INTERNAL_SERVER_ERROR, t.getMessage());
    }
    return resultNode;
  }

  JsonNode getSlowQueries(Universe universe) {
    JsonNode resultNode;
    try {
      resultNode = queryHelper.slowQueries(universe);
    } catch (NullPointerException e) {
      LOG.error("Universe does not have a private IP or DNS", e);
      throw new YWServiceException(INTERNAL_SERVER_ERROR, "Universe failed to fetch slow queries");
    } catch (Throwable t) {
      LOG.error("Error retrieving queries for universe", t);
      throw new YWServiceException(INTERNAL_SERVER_ERROR, t.getMessage());
    }
    return resultNode;
  }

  JsonNode resetSlowQueries(Universe universe) {
    try {
      JsonNode resultNode = queryHelper.resetQueries(universe);
      return resultNode;
    } catch (NullPointerException e) {
      throw new YWServiceException(INTERNAL_SERVER_ERROR, "Failed reach node, invalid IP or DNS.");
    } catch (Throwable t) {
      LOG.error("Error resetting slow queries for universe", t);
      throw new YWServiceException(INTERNAL_SERVER_ERROR, t.getMessage());
    }
  }
}
