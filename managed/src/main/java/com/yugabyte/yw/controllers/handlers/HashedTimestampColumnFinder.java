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

import static com.yugabyte.yw.common.TableSpaceStructures.HashedTimestampColumnFinderResponse;
import static com.yugabyte.yw.common.TableSpaceStructures.QueryUniverseDBListResponse;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.fasterxml.jackson.core.type.TypeReference;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import java.util.Random;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Singleton
public class HashedTimestampColumnFinder {

  private NodeUniverseManager nodeUniverseManager;

  @Inject
  public HashedTimestampColumnFinder(NodeUniverseManager nodeUniverseManager) {
    this.nodeUniverseManager = nodeUniverseManager;
  }

  private static final String HASHED_TIMESTAMP_COLUMN_STATEMENT =
      "select jsonb_agg(t) from "
          + "(select current_database(), c.relname as table_name, h.relname as index_name, "
          + "h.pg_get_indexdef as index_command, 'perf_tuning' as comment from "
          + "(select relname, i.indrelid tableid, "
          + "i.indoption, pg_get_indexdef(i.indexrelid), a.attname from pg_class c, "
          + "pg_attribute a, pg_index i where c.oid = i.indexrelid and a.attrelid = c.oid "
          + "and a.atttypid in (1114, 1184) and pg_get_indexdef(i.indexrelid) "
          + "like '%' || a.attname || ' HASH%') h, pg_class c where c.oid = h.tableid) as t;";

  private static final String DBLIST_STATEMENT =
      "select jsonb_agg(t) from (select datname from pg_database where datname "
          + "not in ('template0', 'template1', 'system_platform', 'postgres')) as t;";

  private static final String DEFAULT_DB_NAME = "yugabyte";

  private NodeDetails getRandomLiveTServer(List<NodeDetails> filteredServers) {
    if (filteredServers.isEmpty()) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "No live TServers for getRandomLiveTServer");
    }
    Random random = new Random();
    return filteredServers.get(random.nextInt(filteredServers.size()));
  }

  public List<HashedTimestampColumnFinderResponse> getHashedTimestampColumns(Universe universe) {

    NodeDetails randomTServer = getRandomLiveTServer(universe.getLiveTServersInPrimaryCluster());

    ShellResponse getDB =
        nodeUniverseManager.runYsqlCommand(
            randomTServer, universe, DEFAULT_DB_NAME, DBLIST_STATEMENT);

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
    log.trace("getDBLIST: {}", getDBList);

    boolean parseDBSuccess = false;
    try {
      ObjectMapper objectMapper = new ObjectMapper();
      List<QueryUniverseDBListResponse> universeDBList =
          objectMapper.readValue(
              getDBList, new TypeReference<List<QueryUniverseDBListResponse>>() {});
      parseDBSuccess = true;

      List<HashedTimestampColumnFinderResponse> hashedTimestampResponse = new ArrayList<>();

      // TODO: modify approach when new variant of runYsqlCommand is available to take list of DBs
      for (QueryUniverseDBListResponse dbname : universeDBList) {

        ShellResponse response =
            nodeUniverseManager.runYsqlCommand(
                randomTServer, universe, dbname.datname, HASHED_TIMESTAMP_COLUMN_STATEMENT);

        String responseJSON = CommonUtils.extractJsonisedSqlResponse(response);
        // responseJSON unfortunately seems to return a length 1 empty string rather than null when
        // given 0 rows, so .isEmpty() is insufficient.
        if (responseJSON == null || responseJSON.length() <= 1) {
          continue;
        }

        log.trace(
            "Hashed timestamp indexes for node {}, database {}: {} -- json: {}, json length: {}",
            randomTServer.nodeName,
            CommonUtils.logTableName(dbname.datname),
            response.message,
            responseJSON,
            responseJSON.length());
        // Accumulate all entries for database into universe's list of hashed timestamp columns.
        hashedTimestampResponse.addAll(
            objectMapper.readValue(
                responseJSON, new TypeReference<List<HashedTimestampColumnFinderResponse>>() {}));
      }

      for (HashedTimestampColumnFinderResponse res : hashedTimestampResponse) {
        log.trace(
            "Final JSON responses: current_database: {}, "
                + "table_name: {}, index_name: {}, index_command: {}, description: {}",
            res.currentDatabase,
            CommonUtils.logTableName(res.tableName),
            res.indexName,
            res.indexCommand,
            res.description);
      }

      return hashedTimestampResponse;

    } catch (IOException ioe) {
      if (parseDBSuccess) {
        String err = "Error while parsing hashed timestamp columns for databases: " + getDBList;
        log.error(err, ioe);
        throw new PlatformServiceException(INTERNAL_SERVER_ERROR, err);
      }
      String err = "Error while parsing database list for databases: " + getDBList;
      log.error(err, ioe);
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, err);
    }
  }
}
