// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.fasterxml.jackson.core.type.TypeReference;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.TableSpaceStructures.PlacementBlock;
import com.yugabyte.yw.common.TableSpaceStructures.TableSpaceInfo;
import com.yugabyte.yw.common.TableSpaceStructures.TableSpaceQueryResponse;
import com.yugabyte.yw.common.TableSpaceUtil;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.time.Duration;
import java.time.Instant;
import java.util.ArrayList;
import java.util.Collection;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.function.Function;
import java.util.regex.Pattern;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.Value;
import lombok.extern.slf4j.Slf4j;
import play.libs.Json;

@Slf4j
public class CreateTableSpaces extends AbstractTaskBase {

  private static final Pattern YSQLSH_CREATE_TABLESPACE_SUCCESS =
      Pattern.compile("Command output:.*CREATE TABLESPACE", Pattern.DOTALL);
  static final String FETCH_TABLESPACES_QUERY =
      "select jsonb_agg(t) from (select spcname, spcoptions from pg_catalog.pg_tablespace) as t";

  private final NodeUniverseManager nodeUniverseManager;

  @Inject
  protected CreateTableSpaces(
      BaseTaskDependencies baseTaskDependencies, NodeUniverseManager nodeUniverseManager) {
    super(baseTaskDependencies);
    this.nodeUniverseManager = nodeUniverseManager;
  }

  public static class Params extends UniverseTaskParams {
    // List of tablespaces to be created.
    public List<TableSpaceInfo> tablespaceInfos;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    log.info("Running {}", getName());
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());

    boolean tablespacesCreated = false;
    String lastError = null;
    int attempt = 0;
    Duration retryDelay =
        confGetter.getConfForScope(universe, UniverseConfKeys.createTablespacesRetryDelay);
    Duration retryTimeout =
        confGetter.getConfForScope(universe, UniverseConfKeys.createTablespacesRetryTimeout);
    Integer minRetries =
        confGetter.getConfForScope(universe, UniverseConfKeys.createTablespacesMinRetries);
    Instant timeout = Instant.now().plus(retryTimeout);
    while (Instant.now().isBefore(timeout) || attempt < minRetries) {
      NodeDetails randomTServer = null;
      try {
        randomTServer = CommonUtils.getARandomLiveTServer(universe);
      } catch (IllegalStateException ise) {
        lastError = "Cluster may not have been initialized yet.";
        log.warn("{} attempt to create tablespace failed - {}", ++attempt, lastError);
        waitFor(retryDelay);
        continue;
      }

      try {
        // Fetching existing tablespaces.
        ShellResponse shellResponse =
            nodeUniverseManager
                .runYsqlCommand(randomTServer, universe, "postgres", FETCH_TABLESPACES_QUERY)
                .processErrors();

        Map<String, TableSpaceInfo> existingTablespaces = new HashMap<>();
        String jsonData = CommonUtils.extractJsonisedSqlResponse(shellResponse);
        if (jsonData != null && !jsonData.isEmpty()) {
          try {
            ObjectMapper objectMapper = new ObjectMapper();
            List<TableSpaceQueryResponse> tablespaceList =
                objectMapper.readValue(
                    jsonData, new TypeReference<List<TableSpaceQueryResponse>>() {});
            existingTablespaces =
                tablespaceList.stream()
                    .map(TableSpaceUtil::parseToTableSpaceInfo)
                    .collect(Collectors.toMap(tsi -> tsi.name, Function.identity()));
          } catch (Exception e) {
            lastError = "Unable to parse fetchTablespaceQuery response " + jsonData;
            log.warn("{} attempt to create tablespace failed - {}", ++attempt, lastError, e);
            waitFor(retryDelay);
            continue;
          }
        }

        // Checking if we already have some tablespaces created.
        Collection<TableSpaceInfo> tablespacesToCreate = new ArrayList<>();
        for (TableSpaceInfo tsi : taskParams().tablespaceInfos) {
          TableSpaceInfo existingTSI = existingTablespaces.get(tsi.name);
          if (existingTSI != null) {
            if (!existingTSI.equals(tsi)) {
              String msg =
                  String.format(
                      "Unable to create tablespace as another tablespace with"
                          + " the same name '%s' exists",
                      tsi.name);
              log.warn(msg);
              throw new PlatformServiceException(INTERNAL_SERVER_ERROR, msg);
            }
            log.info("Skipping creation of tablespace '{}' - already created.", tsi.name);
          } else {
            tablespacesToCreate.add(tsi);
          }
        }

        // Creating tablespaces.
        for (TableSpaceInfo tsi : tablespacesToCreate) {
          log.info("Creating tablespace '{}'", tsi.name);
          String createTablespaceQuery =
              String.format(
                  "CREATE TABLESPACE %s WITH (replica_placement='%s');",
                  tsi.name,
                  Json.stringify(
                      Json.toJson(new ReplicaPlacement(tsi.numReplicas, tsi.placementBlocks))));
          ShellResponse response =
              nodeUniverseManager
                  .runYsqlCommand(randomTServer, universe, "postgres", createTablespaceQuery)
                  .processErrors();

          if (!response.isSuccess()
              || !YSQLSH_CREATE_TABLESPACE_SUCCESS.matcher(response.getMessage()).find()) {
            lastError =
                "Create tablespace query failure response "
                    + response.code
                    + ":"
                    + response.message;
            log.warn(
                "{} attempt to create tablespace via node {} failed - {}",
                ++attempt,
                randomTServer.nodeName,
                lastError);
            waitFor(retryDelay);
          }
        }
        tablespacesCreated = true;
        break;
      } catch (RuntimeException e) {
        if (e instanceof PlatformServiceException) {
          throw e;
        }
        lastError = "Error while executing SQL request: " + e.getMessage();
        log.warn("{} attempt to create tablespace failed - {}", ++attempt, lastError, e);
        waitFor(retryDelay);
      }
    }
    if (!tablespacesCreated) {
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR,
          "All " + attempt + " attempts to create tablespaces failed: " + lastError);
    }
    log.info("Completed {}", getName());
  }

  @Value
  private static class ReplicaPlacement {
    public int num_replicas;
    public List<PlacementBlock> placement_blocks;
  }
}
