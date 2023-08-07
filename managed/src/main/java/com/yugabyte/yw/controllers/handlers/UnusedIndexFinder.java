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

import static com.yugabyte.yw.common.TableSpaceStructures.QueryUniverseDBListResponse;
import static com.yugabyte.yw.common.TableSpaceStructures.UnusedIndexFinderResponse;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.fasterxml.jackson.core.type.TypeReference;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.PlatformExecutorFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.io.IOException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Random;
import java.util.Set;
import java.util.concurrent.Callable;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.stream.Collectors;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Singleton
public class UnusedIndexFinder {

  public static final String QUERY_EXECUTOR_THREAD_POOL_CONFIG_KEY = "yb.perf_advisor.max_threads";

  private final NodeUniverseManager nodeUniverseManager;
  private final PlatformExecutorFactory platformExecutorFactory;
  private final RuntimeConfGetter confGetter;

  @Inject
  public UnusedIndexFinder(
      NodeUniverseManager nodeUniverseManager,
      PlatformExecutorFactory platformExecutorFactory,
      RuntimeConfGetter confGetter) {
    this.nodeUniverseManager = nodeUniverseManager;
    this.platformExecutorFactory = platformExecutorFactory;
    this.confGetter = confGetter;
  }

  private static final String GET_DB_LIST_STATEMENT =
      "select jsonb_agg(t) from (select datname from pg_database where datname "
          + "not in ('template0', 'template1', 'system_platform', 'postgres')) as t;";

  private static final String GET_UNUSED_INDEXES_STATEMENT =
      "select jsonb_agg(t) from (select current_database(), relname as table_name, "
          + "indexrelname as index_name, pg_get_indexdef(stat.indexrelid) as index_command, "
          + "'perf tuning' as comment from pg_stat_user_indexes stat, pg_index idx where "
          + "stat.indexrelid = idx.indexrelid and idx.indisunique = 'f' and idx_scan = 0) as t;";

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
    int maxParallelThreads = confGetter.getConfForScope(universe, UniverseConfKeys.maxThreads);

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
        ExecutorService threadPool =
            platformExecutorFactory.createFixedExecutor(
                getClass().getSimpleName(), maxParallelThreads, Executors.defaultThreadFactory());
        Set<Future<List<UnusedIndexFinderResponse>>> futures = new HashSet<>();

        try {
          for (NodeDetails liveNode : tserverLiveNodes) {
            Callable<List<UnusedIndexFinderResponse>> callable =
                () -> {
                  ShellResponse response =
                      nodeUniverseManager.runYsqlCommand(
                          liveNode, universe, dbname.datname, GET_UNUSED_INDEXES_STATEMENT);
                  String responseJSON = CommonUtils.extractJsonisedSqlResponse(response);

                  // responseJSON unfortunately seems to return a length 1 empty string rather than
                  // null when given 0 rows, so .isEmpty() is insufficient.
                  if (responseJSON == null || responseJSON.length() <= 1) {
                    return null;
                  }

                  return objectMapper.readValue(
                      responseJSON, new TypeReference<List<UnusedIndexFinderResponse>>() {});
                };

            Future<List<UnusedIndexFinderResponse>> future = threadPool.submit(callable);
            futures.add(future);
          }
          if (futures.isEmpty()) {
            throw new IllegalStateException("None of the nodes are accessible.");
          } else if (futures.size() != tserverLiveNodes.size()) {
            throw new IllegalStateException(
                "Failing the operation since some of the nodes are not accessible.");
          }

          List<UnusedIndexFinderResponse> unusedIndexesAcrossNodes =
              new ArrayList<UnusedIndexFinderResponse>();
          try {
            for (Future<List<UnusedIndexFinderResponse>> future : futures) {
              if (future.get() != null) {
                unusedIndexesAcrossNodes.addAll(future.get());
              }
            }
          } catch (InterruptedException e) {
            log.error("Error fetching unused index data", e);
          } catch (ExecutionException e) {
            log.error("Error fetching unused index data", e.getCause());
          }

          Map<String, List<UnusedIndexFinderResponse>> unusedIndexesByName =
              unusedIndexesAcrossNodes.stream().collect(Collectors.groupingBy(i -> i.indexName));
          for (Map.Entry<String, List<UnusedIndexFinderResponse>> unusedIndexes :
              unusedIndexesByName.entrySet()) {
            if (unusedIndexes.getValue().size() == tserverLiveNodes.size()) {
              unusedIndexResponse.add(unusedIndexes.getValue().get(0));
            }
          }
        } finally {
          threadPool.shutdown();
        }
      }

      for (UnusedIndexFinderResponse res : unusedIndexResponse) {
        log.trace(
            "Final JSON responses: current_database: {}, table_name: {}, "
                + "index_name: {}, index_command: {}, description: {}",
            res.currentDatabase,
            CommonUtils.logTableName(res.tableName),
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
