// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.core.type.TypeReference;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.yugabyte.yw.common.TableSpaceStructures.PlacementBlock;
import com.yugabyte.yw.common.TableSpaceStructures.TableSpaceInfo;
import com.yugabyte.yw.common.TableSpaceStructures.TableSpaceOptions;
import com.yugabyte.yw.common.TableSpaceStructures.TableSpaceQueryResponse;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.forms.CreateTablespaceParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.PlacementInfo.PlacementAZ;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;
import java.util.function.Function;
import java.util.regex.Pattern;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

@Slf4j
public class TableSpaceUtil {

  public static final String DB = "postgres";

  public static final String REPLICA_PLACEMENT_TEXT = "replica_placement=";

  public static final String FETCH_TABLESPACES_QUERY =
      wrapInJson("select spcname, spcoptions from pg_catalog.pg_tablespace");

  public static final String FETCH_TABLES_WITH_TABLESPACES_QUERY =
      wrapInJson(
          "SELECT c.relfilenode, t.spcname, c.relname FROM pg_tablespace t JOIN pg_class c ON t.oid"
              + " = c.reltablespace");

  private static final Pattern VALID_POSTGRES_IDENTIFIER =
      Pattern.compile("^[a-z_][a-z0-9_]*$", Pattern.CASE_INSENSITIVE);

  private static final int MAX_IDENTIFIER_LENGTH = 63;

  private static String wrapInJson(String query) {
    return "select jsonb_agg(t) from (" + query + ") as t";
  }

  public static boolean isValidTablespaceName(String name) {
    if (name != null) {
      name = name.trim();
    }
    if (StringUtils.isEmpty(name)) {
      return false;
    }
    if (name.length() > MAX_IDENTIFIER_LENGTH) {
      return false;
    }
    if (!VALID_POSTGRES_IDENTIFIER.matcher(name).matches()) {
      return false;
    }
    return true;
  }

  public static Optional<PlacementBlock> findPlacementBlockByNode(
      TableSpaceInfo tableSpaceInfo, NodeDetails node) {
    if (tableSpaceInfo == null || tableSpaceInfo.placementBlocks == null) {
      return Optional.empty();
    }
    return tableSpaceInfo.placementBlocks.stream()
        .filter(pb -> pb.cloud.equals(node.cloudInfo.cloud))
        .filter(pb -> pb.region.equals(node.cloudInfo.region))
        .filter(pb -> pb.zone.equals(node.cloudInfo.az))
        .findFirst();
  }

  public static boolean isSameAz(PlacementBlock pb, AvailabilityZone az) {
    return pb.zone.equals(az.getCode())
        && pb.region.equals(az.getRegion().getCode())
        && pb.cloud.equals(az.getRegion().getProvider().getCode());
  }

  public static TableSpaceInfo parseToTableSpaceInfo(TableSpaceQueryResponse tablespace) {
    TableSpaceInfo.TableSpaceInfoBuilder builder = TableSpaceInfo.builder();
    builder.name(tablespace.tableSpaceName);
    try {
      ObjectMapper objectMapper = new ObjectMapper();
      if (tablespace.tableSpaceOptions != null) {
        for (String optionStr : tablespace.tableSpaceOptions) {
          if (optionStr.startsWith(REPLICA_PLACEMENT_TEXT)) {
            optionStr = optionStr.replaceFirst(REPLICA_PLACEMENT_TEXT, "");
            TableSpaceOptions option = objectMapper.readValue(optionStr, TableSpaceOptions.class);
            builder.numReplicas(option.numReplicas).placementBlocks(option.placementBlocks);
          } else {
            String msg = "Syntax error in the tablespace 'spcoptions' section.";
            log.error(msg);
            throw new PlatformServiceException(INTERNAL_SERVER_ERROR, msg);
          }
        }
      }
      return builder.build();
    } catch (IOException ioe) {
      log.error(
          "Unable to parse options fron fetchTablespaceQuery response {}",
          tablespace.tableSpaceOptions,
          ioe);
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR, "Error while fetching TableSpace information");
    }
  }

  public static void validateTablespaces(
      CreateTablespaceParams tablespacesInfo, Universe universe) {
    if (tablespacesInfo == null
        || tablespacesInfo.tablespaceInfos == null
        || tablespacesInfo.tablespaceInfos.size() == 0) {
      String msg = String.format("No information about tablespaces was found");
      log.warn(msg);
      throw new PlatformServiceException(BAD_REQUEST, msg);
    }
    for (TableSpaceInfo tsInfo : tablespacesInfo.tablespaceInfos) {
      validateTablespace(tsInfo, universe);
    }
  }

  public static void validateTablespace(TableSpaceInfo tsInfo, Universe universe) {
    PlacementInfo primaryClusterPlacement =
        universe.getUniverseDetails().getPrimaryCluster().getOverallPlacement();
    validateTablespace(tsInfo, primaryClusterPlacement);
  }

  public static void validateTablespace(TableSpaceInfo tsInfo, PlacementInfo placementInfo) {
    Set<Pair<String, Pair<String, String>>> crzs = new HashSet<>();
    List<Integer> leaderPreferences = new ArrayList<>();
    int numReplicas = 0;
    for (PlacementBlock pb : tsInfo.placementBlocks) {
      numReplicas += pb.minNumReplicas;
      Pair<String, Pair<String, String>> crz = new Pair<>(pb.cloud, new Pair<>(pb.region, pb.zone));
      if (crzs.contains(crz)) {
        String msg =
            String.format(
                "Duplicate placement for cloud %s, region %s, zone %s",
                pb.cloud, pb.region, pb.zone);
        log.warn(msg);
        throw new PlatformServiceException(BAD_REQUEST, msg);
      }
      crzs.add(crz);
      validatePlacement(placementInfo, pb);

      if (pb.leaderPreference != null) {
        leaderPreferences.add(pb.leaderPreference);
      }
    }

    // Validating correctness of leader preference values.
    // Current rules are:
    //   - leader_preference values have to be >=1;
    //   - Zones can have no leader_preference set;
    //   - leader_preference values can repeat;
    //   - leader_preference values have to be continuous:
    //     Examples:
    //        za:1, zb, zc -> ok
    //        za:1, zb:1, zc:2, zd:3, ze -> ok
    //        za:1, zb:3 -> not ok as we have 3 but 2 is missing
    // - The minimal value should be 1.
    Collections.sort(leaderPreferences);
    if (!leaderPreferences.isEmpty() && leaderPreferences.get(0) > 1) {
      String msg = "Invalid first leader preference value (should be 1)";
      log.warn(msg);
      throw new PlatformServiceException(BAD_REQUEST, msg);
    }

    for (int i = 0; i < leaderPreferences.size() - 1; i++) {
      if (leaderPreferences.get(i + 1) - leaderPreferences.get(i) > 1) {
        String msg =
            String.format(
                "Invalid leader preferences order (current value %d, next value %d)",
                leaderPreferences.get(i), leaderPreferences.get(i + 1));
        log.warn(msg);
        throw new PlatformServiceException(BAD_REQUEST, msg);
      }
    }

    if (tsInfo.numReplicas != numReplicas) {
      log.warn(
          "Invalid number of replicas in tablespace {}. Number in info {}, "
              + "number in placement blocks {}",
          tsInfo.name,
          tsInfo.numReplicas,
          numReplicas);
      throw new PlatformServiceException(
          BAD_REQUEST, "Invalid number of replicas in tablespace " + tsInfo.name);
    }
  }

  public static Map<String, TableSpaceInfo> getCurrentTablespaces(
      NodeDetails nodeToQuery, Universe universe, NodeUniverseManager nodeUniverseManager) {
    ShellResponse shellResponse =
        nodeUniverseManager
            .runYsqlCommand(nodeToQuery, universe, DB, FETCH_TABLESPACES_QUERY)
            .processErrors();

    Map<String, TableSpaceInfo> existingTablespaces = new HashMap<>();
    String jsonData = CommonUtils.extractJsonisedSqlResponse(shellResponse);
    if (jsonData != null && !jsonData.isEmpty()) {
      ObjectMapper objectMapper = new ObjectMapper();
      try {
        List<TableSpaceQueryResponse> tablespaceList =
            objectMapper.readValue(jsonData, new TypeReference<List<TableSpaceQueryResponse>>() {});
        existingTablespaces =
            tablespaceList.stream()
                .map(TableSpaceUtil::parseToTableSpaceInfo)
                .collect(Collectors.toMap(tsi -> tsi.name, Function.identity()));
      } catch (JsonProcessingException e) {
        String error = "Unable to parse fetchTablespaceQuery response " + jsonData;
        log.error(error, e);
        throw new RuntimeException(error, e);
      }
    }
    return existingTablespaces;
  }

  public static TableSpaceInfo partitionToTablespace(
      UniverseDefinitionTaskParams.PartitionInfo partitionInfo) {
    TableSpaceStructures.TableSpaceInfo tableSpaceInfo = new TableSpaceStructures.TableSpaceInfo();
    tableSpaceInfo.name = partitionInfo.getTablespaceName();
    tableSpaceInfo.numReplicas = partitionInfo.getReplicationFactor();
    List<TableSpaceStructures.PlacementBlock> blocks = new ArrayList<>();
    for (PlacementInfo.PlacementCloud placementCloud : partitionInfo.getPlacement().cloudList) {
      for (PlacementInfo.PlacementRegion placementRegion : placementCloud.regionList) {
        for (PlacementInfo.PlacementAZ placementAZ : placementRegion.azList) {
          AvailabilityZone zone = AvailabilityZone.getOrBadRequest(placementAZ.uuid);
          TableSpaceStructures.PlacementBlock block = new TableSpaceStructures.PlacementBlock();
          block.minNumReplicas = placementAZ.replicationFactor;
          block.zone = zone.getCode();
          block.region = placementRegion.code;
          block.cloud = placementCloud.code;
          if (placementAZ.leaderPreference > 0) {
            block.leaderPreference = placementAZ.leaderPreference;
          }
          blocks.add(block);
        }
      }
    }
    tableSpaceInfo.placementBlocks = blocks;
    return tableSpaceInfo;
  }

  private static void validatePlacement(PlacementInfo clusterPlacement, PlacementBlock pb) {
    PlacementAZ foundAZ =
        clusterPlacement.cloudList.stream()
            .filter(c -> Objects.equals(c.code, pb.cloud))
            .flatMap(c -> c.regionList.stream())
            .filter(r -> Objects.equals(r.code, pb.region))
            .flatMap(r -> r.azList.stream())
            .filter(az -> Objects.equals(az.name, pb.zone))
            .findFirst()
            .orElse(null);
    if (foundAZ == null) {
      String msg =
          String.format(
              "Invalid placement specified by cloud %s, region %s, zone %s",
              pb.cloud, pb.region, pb.zone);
      log.warn(msg);
      throw new PlatformServiceException(BAD_REQUEST, msg);
    }
    if (foundAZ.numNodesInAZ < pb.minNumReplicas) {
      String msg =
          String.format(
              "Placement in cloud %s, region %s, zone %s doesn't have enough nodes",
              pb.cloud, pb.region, pb.zone);
      log.warn(msg);
      throw new PlatformServiceException(BAD_REQUEST, msg);
    }
  }
}
