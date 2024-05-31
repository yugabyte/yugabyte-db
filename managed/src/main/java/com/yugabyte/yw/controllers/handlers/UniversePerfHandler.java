/*
 * Copyright 2022 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */
package com.yugabyte.yw.controllers.handlers;

import static java.lang.Math.max;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.fasterxml.jackson.core.type.TypeReference;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.TableSpaceStructures;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.QueryDistributionSuggestionResponse;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.io.IOException;
import java.time.Duration;
import java.time.OffsetDateTime;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.LinkedList;
import java.util.List;
import java.util.Map;
import java.util.Queue;
import java.util.UUID;
import java.util.regex.Pattern;
import java.util.stream.Collectors;
import lombok.AllArgsConstructor;
import lombok.extern.slf4j.Slf4j;

@Singleton
@Slf4j
public class UniversePerfHandler {

  private NodeUniverseManager nodeUniverseManager;

  @Inject
  public UniversePerfHandler(NodeUniverseManager nodeUniverseManager) {
    this.nodeUniverseManager = nodeUniverseManager;
  }

  private static final String DEFAULT_DATABASE = "postgres";

  // Query distribution suggestion parameters and variables.
  private static final Duration QUERY_DISTRIBUTION_SUGGESTION_INTERVAL = Duration.ofMinutes(60);
  private static final int QUERY_DISTRIBUTION_THRESHOLD_PERCENTAGE = 50;
  private static final int MINIMUM_TOTAL_QUERY_THRESHOLD_FOR_SUGGESTIONS = 1000;
  private static final Pattern SELECT_PATTERN = Pattern.compile("select", Pattern.CASE_INSENSITIVE);
  private static final Pattern DELETE_PATTERN = Pattern.compile("delete", Pattern.CASE_INSENSITIVE);
  private static final Pattern UPDATE_PATTERN = Pattern.compile("update", Pattern.CASE_INSENSITIVE);
  private static final Pattern INSERT_PATTERN = Pattern.compile("insert", Pattern.CASE_INSENSITIVE);
  private static final Pattern WRITE_READ_TEST_PATTERN =
      Pattern.compile("write_read_test", Pattern.CASE_INSENSITIVE);
  private Map<UUID, Map<String, Queue<QueryDistributionTimestampedData>>>
      queryDistributionSnapshots = new HashMap<>();

  public QueryDistributionSuggestionResponse universeQueryDistributionSuggestion(
      Universe universe) {
    UniverseDefinitionTaskParams.Cluster primaryCluster =
        universe.getUniverseDetails().getPrimaryCluster();
    List<NodeDetails> liveNodes =
        universe.getUniverseDetails().getNodesInCluster(primaryCluster.uuid).stream()
            .filter(nodeDetails -> nodeDetails.state == NodeDetails.NodeState.Live)
            .collect(Collectors.toList());

    final String query =
        "select jsonb_agg(t) from (select calls, query from pg_stat_statements) as t;";

    List<QueryDistributionSuggestionResponse.NodeQueryDistributionDetails>
        nodeQueryDistributionDetailsList = new ArrayList<>();

    OffsetDateTime now = getCurrentOffsetDateTime();
    OffsetDateTime queueTimestampsExpiryTime =
        now.minusMinutes(QUERY_DISTRIBUTION_SUGGESTION_INTERVAL.toMinutes());

    int totalQueriesForUniverse = 0;
    OffsetDateTime startTime = null;

    queryDistributionSnapshots.putIfAbsent(universe.getUniverseUUID(), new HashMap<>());
    Map<String, Queue<QueryDistributionTimestampedData>> m =
        queryDistributionSnapshots.get(universe.getUniverseUUID());

    // Iterate over nodes and find number of queries of each type for each node.
    for (NodeDetails liveNode : liveNodes) {
      int numInsert = 0;
      int numUpdate = 0;
      int numSelect = 0;
      int numDelete = 0;
      ShellResponse shellResponse =
          nodeUniverseManager.runYsqlCommand(liveNode, universe, DEFAULT_DATABASE, query);
      String jsonData = CommonUtils.extractJsonisedSqlResponse(shellResponse);

      if (jsonData == null || jsonData.isEmpty()) {
        log.error(
            "Got empty query response while fetching query load for node {} in universe {}",
            liveNode.getNodeName(),
            universe.getUniverseUUID());
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR,
            "Got empty query response while fetching query distribution for node "
                + liveNode.getNodeName());
      }

      try {
        ObjectMapper objectMapper = new ObjectMapper();
        List<TableSpaceStructures.QueryDistributionAcrossNodesResponse>
            queryDistributionAcrossNodesResponseList =
                objectMapper.readValue(
                    jsonData,
                    new TypeReference<
                        List<TableSpaceStructures.QueryDistributionAcrossNodesResponse>>() {});
        for (TableSpaceStructures.QueryDistributionAcrossNodesResponse response :
            queryDistributionAcrossNodesResponseList) {
          if (WRITE_READ_TEST_PATTERN.matcher(response.query).find()) {
            // Ignore write_read_test queries to avoid the skewness % getting affected because a
            // large constant amount.
            continue;
          } else if (INSERT_PATTERN.matcher(response.query).find()) {
            numInsert += response.calls;
          } else if (UPDATE_PATTERN.matcher(response.query).find()) {
            numUpdate += response.calls;
          } else if (DELETE_PATTERN.matcher(response.query).find()) {
            numDelete += response.calls;
          } else if (SELECT_PATTERN.matcher(response.query).find()) {
            numSelect += response.calls;
          }
        }
      } catch (IOException ioe) {
        log.error(
            "Unable to parse universeQueryDistributionSuggestion query response {}", jsonData, ioe);
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR, "Error while parsing query distribution response for universe");
      }

      QueryDistributionTimestampedData queryDistributionTimestampedData =
          new QueryDistributionTimestampedData(now, numSelect, numInsert, numUpdate, numDelete);

      m.putIfAbsent(liveNode.getNodeName(), new LinkedList<>());

      Queue<QueryDistributionTimestampedData> existingQueryDistributionTimestampedData =
          m.get(liveNode.getNodeName());
      QueryDistributionTimestampedData olderTimestampData = null;

      // Remove entries from queue older than 1 hour (queryDistributionSuggestionInterval) to
      // calculate number of operations in last 1 hour on a best-effort basis.
      while (existingQueryDistributionTimestampedData.peek() != null
          && existingQueryDistributionTimestampedData
                  .peek()
                  .timestamp
                  .compareTo(queueTimestampsExpiryTime)
              < 0) {
        startTime = existingQueryDistributionTimestampedData.peek().timestamp;
        olderTimestampData = existingQueryDistributionTimestampedData.poll();
      }
      if (olderTimestampData != null) {
        numInsert -= olderTimestampData.numInsert;
        numSelect -= olderTimestampData.numSelect;
        numUpdate -= olderTimestampData.numUpdate;
        numDelete -= olderTimestampData.numDelete;
      }
      totalQueriesForUniverse += numInsert + numDelete + numSelect + numUpdate;

      QueryDistributionSuggestionResponse.NodeQueryDistributionDetails
          nodeQueryDistributionDetails =
              QueryDistributionSuggestionResponse.NodeQueryDistributionDetails.builder()
                  .node(liveNode.getNodeName())
                  .numInsert(numInsert)
                  .numSelect(numSelect)
                  .numUpdate(numUpdate)
                  .numDelete(numDelete)
                  .build();
      nodeQueryDistributionDetailsList.add(nodeQueryDistributionDetails);

      m.get(liveNode.getNodeName()).add(queryDistributionTimestampedData);
    }

    QueryDistributionSuggestionResponse.QueryDistributionSuggestionResponseBuilder
        queryDistributionSuggestionResponseBuilder =
            QueryDistributionSuggestionResponse.builder()
                .adviceType(AdviceType.QueryDistributionSkew.getAdviceType())
                .details(nodeQueryDistributionDetailsList)
                .startTime((startTime != null) ? startTime.toString() : null)
                .endTime(now.toString());

    if (liveNodes.size() == 1) {
      // No query distribution skewness can be there for single node universes.
      return queryDistributionSuggestionResponseBuilder.build();
    }

    // Find skewness in query load distribution amongst the nodes, if any.
    // Note: We are finding and reporting the node with maximum load compared to average of other
    // nodes.

    double maxPercentageHigherThanAverage = -1;
    String mostHeavilyLoadedNode = null;

    for (QueryDistributionSuggestionResponse.NodeQueryDistributionDetails
        nodeQueryDistributionDetails : nodeQueryDistributionDetailsList) {
      int totalQueriesForNode =
          nodeQueryDistributionDetails.getNumInsert()
              + nodeQueryDistributionDetails.getNumDelete()
              + nodeQueryDistributionDetails.getNumSelect()
              + nodeQueryDistributionDetails.getNumUpdate();
      double averageOfOtherNodes =
          ((double) totalQueriesForUniverse - (double) totalQueriesForNode)
              / (liveNodes.size() - 1);

      if ((averageOfOtherNodes > totalQueriesForNode)
          || (totalQueriesForNode < MINIMUM_TOTAL_QUERY_THRESHOLD_FOR_SUGGESTIONS)) {
        continue;
      }

      double percentageHigherThanAverage =
          (((double) totalQueriesForNode - averageOfOtherNodes) / averageOfOtherNodes) * 100;

      if (percentageHigherThanAverage
          > max(QUERY_DISTRIBUTION_THRESHOLD_PERCENTAGE, maxPercentageHigherThanAverage)) {
        mostHeavilyLoadedNode = nodeQueryDistributionDetails.getNode();
        maxPercentageHigherThanAverage = percentageHigherThanAverage;
      }
    }

    if (mostHeavilyLoadedNode != null) {
      queryDistributionSuggestionResponseBuilder.description(
          String.format(
              "Node %s processed %.2f%% more queries than average of other %d nodes.",
              mostHeavilyLoadedNode, maxPercentageHigherThanAverage, liveNodes.size() - 1));
      queryDistributionSuggestionResponseBuilder.suggestion(
          "Redistribute queries to other nodes in the cluster");
    }

    return queryDistributionSuggestionResponseBuilder.build();
  }

  @AllArgsConstructor
  private static class QueryDistributionTimestampedData {

    private final OffsetDateTime timestamp;
    private final int numSelect;
    private final int numInsert;
    private final int numUpdate;
    private final int numDelete;
  }

  public enum AdviceType {
    QueryDistributionSkew("Query Load Skew");

    private final String adviceType;

    AdviceType(String adviceType) {
      this.adviceType = adviceType;
    }

    public String getAdviceType() {
      return adviceType;
    }
  }

  public OffsetDateTime getCurrentOffsetDateTime() {
    return OffsetDateTime.now();
  }
}
