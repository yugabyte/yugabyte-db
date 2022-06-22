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

import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;
import static com.yugabyte.yw.common.TableSpaceStructures.UnusedIndexFinderResponse;
import static com.yugabyte.yw.common.TableSpaceStructures.QueryUniverseDBListResponse;

import com.fasterxml.jackson.core.type.TypeReference;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.io.IOException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Random;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class UnusedIndexFinder {

  private NodeUniverseManager nodeUniverseManager;

  @Inject
  public UnusedIndexFinder(NodeUniverseManager nodeUniverseManager) {
    this.nodeUniverseManager = nodeUniverseManager;
  }

  private static final String GET_DB_LIST_STATEMENT =
      "select jsonb_agg(t) from (select datname from pg_database where datname "
          + "not in ('template0', 'template1', 'system_platform', 'postgres')) as t;";

  private static final String GET_UNUSED_INDEXES_STATEMENT =
      "select jsonb_agg(t) from (select current_database(), relname as table_name, "
          + "indexrelname as index_name, pg_get_indexdef(indexrelid) as index_command, "
          + "'perf tuning' as comment from pg_stat_user_indexes where idx_scan = 0) as t;";

  private static final String DEFAULT_DB_NAME = "yugabyte";

  private NodeDetails getRandomLiveTServer(List<NodeDetails> filteredServers) {
    if (filteredServers.isEmpty()) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "No live TServers for getRandomLiveTServer");
    }
    Random random = new Random();
    return filteredServers.get(random.nextInt(filteredServers.size()));
  }

  public List<UnusedIndexFinderResponse> getUniverseUnusedIndexes(Universe universe) {

    // TODO: consider having UnusedIndexFinder maintain a state in the event that a crash
    // removes some relevant TServer from the list, and make decisions on more polling rounds.
    List<NodeDetails> tserverLiveNodes = universe.getLiveTServersInPrimaryCluster();

    NodeDetails randomTServer = getRandomLiveTServer(tserverLiveNodes);

    ShellResponse getDB =
        nodeUniverseManager.runYsqlCommand(
            randomTServer, universe, DEFAULT_DB_NAME, GET_DB_LIST_STATEMENT);

    String getDBList = CommonUtils.extractJsonisedSqlResponse(getDB);

    if (getDBList == null || getDBList.isEmpty()) {
      log.error(
          "Got empty response while fetching DB list from node {} in universe {}",
          randomTServer.getNodeName(),
          universe.getUniverseUUID());
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR,
          "Got empty query response while fetching DB list from node "
              + randomTServer.getNodeName());
    }
    log.trace("getDBList: {}", getDBList);

    boolean parseDBSuccess = false;

    try {
      ObjectMapper objectMapper = new ObjectMapper();
      List<QueryUniverseDBListResponse> universeDBList =
          objectMapper.readValue(
              getDBList, new TypeReference<List<QueryUniverseDBListResponse>>() {});
      parseDBSuccess = true;

      List<UnusedIndexFinderResponse> unusedIndexResponse = new ArrayList<>();

      // TODO: Add query batching to cover groups of DB's at a time. Doing so would also
      // mean we need to change how we track entriesAcrossNodes - hash by dbname + index name?
      for (QueryUniverseDBListResponse dbname : universeDBList) {

        HashMap<String, Integer> entriesAcrossNodes = new HashMap<>();

        for (NodeDetails liveNode : tserverLiveNodes) {
          ShellResponse response =
              nodeUniverseManager.runYsqlCommand(
                  liveNode, universe, dbname.datname, GET_UNUSED_INDEXES_STATEMENT);
          String responseJSON = CommonUtils.extractJsonisedSqlResponse(response);

          // responseJSON unfortunately seems to return a length 1 empty string rather than null
          // when given 0 rows, so .isEmpty() is insufficient.
          if (responseJSON == null || responseJSON.length() <= 1) {
            continue;
          }

          List<UnusedIndexFinderResponse> nodeUnusedIndexes = new ArrayList<>();
          nodeUnusedIndexes.addAll(
              objectMapper.readValue(
                  responseJSON, new TypeReference<List<UnusedIndexFinderResponse>>() {}));

          for (UnusedIndexFinderResponse entry : nodeUnusedIndexes) {
            entriesAcrossNodes.merge(entry.indexName, 1, (a, b) -> a + b);

            // Only entries that appear in the query for every node are universally unused.
            if (entriesAcrossNodes.get(entry.indexName) == tserverLiveNodes.size()) {
              unusedIndexResponse.add(entry);
            }
          }
        }
      }

      for (UnusedIndexFinderResponse res : unusedIndexResponse) {
        log.trace(
            "Final JSON responses: current_database: {}, table_name: {}, "
                + "index_name: {}, index_command: {}, description: {}",
            res.currentDatabase,
            res.tableName,
            res.indexName,
            res.indexCommand,
            res.description);
      }

      return unusedIndexResponse;

    } catch (IOException ioe) {
      if (parseDBSuccess) {
        String err = "Error while parsing unused index entries for databases: " + getDBList;
        log.error(err, ioe);
        throw new PlatformServiceException(INTERNAL_SERVER_ERROR, err);
      }
      String err = "Error while parsing database list for databases: " + getDBList;
      log.error(err, ioe);
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, err);
    }
  }
}
