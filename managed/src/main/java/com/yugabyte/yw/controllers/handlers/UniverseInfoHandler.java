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
import static play.mvc.Http.Status.SERVICE_UNAVAILABLE;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.net.HostAndPort;
import com.google.inject.Inject;
import com.yugabyte.yw.cloud.UniverseResourceDetails;
import com.yugabyte.yw.cloud.UniverseResourceDetails.Context;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.HealthChecker;
import com.yugabyte.yw.common.AWSUtil;
import com.yugabyte.yw.common.AZUtil;
import com.yugabyte.yw.common.GCPUtil;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.UniverseInterruptionResult;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.services.YBClientService;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.metrics.MetricQueryHelper;
import com.yugabyte.yw.metrics.MetricQueryResponse;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.HealthCheck;
import com.yugabyte.yw.models.HealthCheck.Details;
import com.yugabyte.yw.models.MasterInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import com.yugabyte.yw.queries.QueryHelper;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.RejectedExecutionException;
import java.util.stream.Collectors;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import org.yb.client.GetMasterRegistrationResponse;
import org.yb.client.YBClient;
import play.libs.Json;

@Slf4j
@Singleton
public class UniverseInfoHandler {

  @Inject private MetricQueryHelper metricQueryHelper;
  @Inject private QueryHelper queryHelper;
  @Inject private RuntimeConfigFactory runtimeConfigFactory;
  @Inject private YBClientService ybService;
  @Inject private NodeUniverseManager nodeUniverseManager;
  @Inject private HealthChecker healthChecker;
  @Inject private AWSUtil awsUtil;
  @Inject private AZUtil azUtil;
  @Inject private GCPUtil gcpUtil;

  public UniverseResourceDetails getUniverseResources(
      Customer customer, UniverseDefinitionTaskParams taskParams) {
    Set<NodeDetails> nodesInCluster;
    if (taskParams
        .getCurrentClusterType()
        .equals(UniverseDefinitionTaskParams.ClusterType.PRIMARY)) {
      nodesInCluster =
          taskParams.nodeDetailsSet.stream()
              .filter(n -> n.isInPlacement(taskParams.getPrimaryCluster().uuid))
              .collect(Collectors.toSet());
    } else {
      nodesInCluster =
          taskParams.nodeDetailsSet.stream()
              .filter(n -> n.isInPlacement(taskParams.getReadOnlyClusters().get(0).uuid))
              .collect(Collectors.toSet());
    }
    UniverseResourceDetails.Context context =
        new Context(runtimeConfigFactory.globalRuntimeConf(), customer, taskParams, true);
    return UniverseResourceDetails.create(nodesInCluster, taskParams, context);
  }

  public List<UniverseResourceDetails> universeListCost(Customer customer) {
    Set<Universe> universeSet = customer.getUniverses();
    if (CollectionUtils.isEmpty(universeSet)) {
      return Collections.emptyList();
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
        log.error(
            "Could not add cost details for Universe with UUID: " + universe.getUniverseUUID());
      }
    }
    return response;
  }

  public JsonNode status(Universe universe) {
    JsonNode result;
    try {

      result = getUniverseAliveStatus(universe, metricQueryHelper);
    } catch (RuntimeException e) {
      // TODO(API) dig deeper and find root cause of RuntimeException
      throw new PlatformServiceException(BAD_REQUEST, e.getMessage());
    }
    if (result.has("error")) {
      throw new PlatformServiceException(BAD_REQUEST, result.get("error"));
    }
    return result;
  }

  public UniverseInterruptionResult spotUniverseStatus(Universe universe) {
    UniverseInterruptionResult result = new UniverseInterruptionResult(universe.getName());
    UserIntent userIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
    switch (userIntent.providerType) {
      case aws:
        result = awsUtil.spotInstanceUniverseStatus(universe);
        break;
      case gcp:
        result = gcpUtil.spotInstanceUniverseStatus(universe);
        break;
      case azu:
        result = azUtil.spotInstanceUniverseStatus(universe);
    }
    return result;
  }

  public List<Details> healthCheck(UUID universeUUID) {
    List<Details> detailsList = new ArrayList<>();
    try {
      List<HealthCheck> checks = HealthCheck.getAll(universeUUID);
      for (HealthCheck check : checks) {
        detailsList.add(check.getDetailsJson());
      }
    } catch (RuntimeException e) {
      // TODO(API) dig deeper and find root cause of RuntimeException
      throw new PlatformServiceException(BAD_REQUEST, e.getMessage());
    }
    return detailsList;
  }

  public void triggerHealthCheck(Customer customer, Universe universe) {
    // We do not OBSERVE the result of the checkSingleUniverse, we are just interested that
    // the health check result is queued.
    healthChecker.checkSingleUniverse(customer, universe);
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
            BAD_REQUEST, "Leader master not found for universe " + universe.getUniverseUUID());
      }
      return leaderMasterHostAndPort;
    } catch (RuntimeException e) {
      throw new PlatformServiceException(BAD_REQUEST, e.getMessage());
    } finally {
      ybService.closeClient(client, hostPorts);
    }
  }

  public List<MasterInfo> getMasterInfos(Universe universe) {
    final String hostPorts = universe.getMasterAddresses();
    String certificate = universe.getCertificateNodetoNode();
    YBClient client = null;
    // Get and return Leader IP
    try {
      client = ybService.getClient(hostPorts, certificate);
      List<GetMasterRegistrationResponse> masterRegistrationResponseList =
          client.getMasterRegistrationResponseList();
      if (masterRegistrationResponseList == null) {
        throw new PlatformServiceException(
            NOT_FOUND,
            "Cannot find master registration list for universe " + universe.getUniverseUUID());
      }
      return masterRegistrationResponseList.stream()
          .map(MasterInfo::convertFrom)
          .collect(Collectors.toList());
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
    } catch (RejectedExecutionException e) {
      log.error(e.getMessage(), e);
      throw new PlatformServiceException(SERVICE_UNAVAILABLE, e.getMessage());
    } catch (PlatformServiceException e) {
      throw e;
    } catch (Exception e) {
      log.error("Error retrieving queries for universe", e);
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, e.getMessage());
    }
    return resultNode;
  }

  public JsonNode getSlowQueries(Universe universe) {
    JsonNode resultNode;
    try {
      resultNode = queryHelper.slowQueries(universe);
    } catch (RejectedExecutionException e) {
      log.error(e.getMessage(), e);
      throw new PlatformServiceException(SERVICE_UNAVAILABLE, e.getMessage());
    } catch (IllegalArgumentException e) {
      log.error(e.getMessage(), e);
      throw new PlatformServiceException(BAD_REQUEST, e.getMessage());
    } catch (PlatformServiceException e) {
      throw e;
    } catch (Exception e) {
      log.error("Error retrieving queries for universe", e);
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, e.getMessage());
    }
    return resultNode;
  }

  public JsonNode resetSlowQueries(Universe universe) {
    try {
      return queryHelper.resetQueries(universe);
    } catch (RejectedExecutionException e) {
      log.error(e.getMessage(), e);
      throw new PlatformServiceException(SERVICE_UNAVAILABLE, e.getMessage());
    } catch (PlatformServiceException e) {
      throw e;
    } catch (Exception e) {
      log.error("Error resetting slow queries for universe", e);
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, e.getMessage());
    }
  }

  public Path downloadNodeLogs(
      Customer customer, Universe universe, NodeDetails node, Path targetFile) {
    nodeUniverseManager.downloadNodeLogs(node, universe, targetFile.toString());
    return targetFile;
  }

  public Path downloadNodeFile(
      Customer customer,
      Universe universe,
      NodeDetails node,
      String ybHomeDir,
      List<String> sourceNodeFile,
      Path targetFile) {
    nodeUniverseManager.downloadNodeFile(
        node, universe, ybHomeDir, sourceNodeFile, targetFile.toString());
    return targetFile;
  }

  private JsonNode getUniverseAliveStatus(Universe universe, MetricQueryHelper metricQueryHelper) {
    List<MetricQueryResponse.Entry> values = new ArrayList<>();
    boolean queryError = false;
    try {
      values =
          metricQueryHelper.queryDirect(
              "max_over_time(up{node_prefix=\""
                  + universe.getUniverseDetails().nodePrefix
                  + "\"}[30s])");
    } catch (RuntimeException re) {
      queryError = true;
      log.debug(
          "Error fetching node status from prometheus for universe {} ",
          universe.getUniverseUUID(),
          re);
    }

    // convert prom query results to Map<hostname -> Map<port -> liveness>>
    Map<String, Map<Integer, Boolean>> nodePortStatus = processNodeUpMetricValues(values);

    // build JSON result node for master/tserver/node liveness
    JsonNode result = convertToNodeStatus(universe, queryError, nodePortStatus);

    return result;
  }

  // Convert map { ip -> port -> boolean alive} to a JSON object that contains
  // node, master, tserver liveness
  private static JsonNode convertToNodeStatus(
      Universe universe, boolean queryError, Map<String, Map<Integer, Boolean>> nodePortStatus) {

    ObjectNode result = Json.newObject();
    result.put("universe_uuid", universe.getUniverseUUID().toString());
    for (final NodeDetails nodeDetails : universe.getNodes()) {

      Map<Integer, Boolean> portStatus =
          nodePortStatus.getOrDefault(nodeDetails.cloudInfo.private_ip, new HashMap<>());
      boolean masterStatus = portStatus.getOrDefault(nodeDetails.masterHttpPort, false);
      boolean tserverStatus = portStatus.getOrDefault(nodeDetails.tserverHttpPort, false);
      boolean nodeStatus = false;
      // check if k8s explicitly
      if (universe.getNodeDeploymentMode(nodeDetails).equals(CloudType.kubernetes)) {
        // in k8s, the master/tserver running means the pod is running
        nodeStatus =
            (nodeDetails.isMaster && masterStatus) || (nodeDetails.isTserver && tserverStatus);
      } else {
        nodeStatus = portStatus.getOrDefault(nodeDetails.nodeExporterPort, false);
      }
      if (!nodeStatus && nodeDetails.isActive()) {
        nodeDetails.state = queryError ? NodeState.MetricsUnavailable : NodeState.Unreachable;
      }

      ObjectNode nodeJson =
          Json.newObject()
              .put("tserver_alive", tserverStatus)
              .put("master_alive", masterStatus)
              .put("node_status", nodeDetails.state.toString());
      result.set(nodeDetails.nodeName, nodeJson);
    }
    return result;
  }

  // Convert 'up' metric results that look like up{instance="ip:port"} = 0/1
  // to a map { ip -> port -> boolean }
  private static Map<String, Map<Integer, Boolean>> processNodeUpMetricValues(
      List<MetricQueryResponse.Entry> values) {
    Map<String, Map<Integer, Boolean>> nodePortStatus = new HashMap<>();

    values.stream()
        .filter(entry -> entry.labels != null && entry.labels.containsKey("instance"))
        .filter(entry -> CollectionUtils.isNotEmpty(entry.values))
        .forEach(
            entry -> {
              try {
                String[] hostPort = entry.labels.get("instance").split(":", 2);
                String host = hostPort[0];
                int port = Integer.parseInt(hostPort[1]);
                boolean isAlive = Util.doubleEquals(entry.values.get(0).getRight(), 1);

                nodePortStatus
                    .computeIfAbsent(host, h -> new HashMap<Integer, Boolean>())
                    .put(port, isAlive);

              } catch (Exception ex) {
                log.debug(
                    "error processing up prometheus metric entry for alive status {} : {}",
                    entry,
                    ex);
                // ignore exceptions
              }
            });

    return nodePortStatus;
  }
}
