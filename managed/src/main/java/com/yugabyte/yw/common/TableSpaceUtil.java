// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.yugabyte.yw.common.TableSpaceStructures.PlacementBlock;
import com.yugabyte.yw.common.TableSpaceStructures.TableSpaceInfo;
import com.yugabyte.yw.common.TableSpaceStructures.TableSpaceOptions;
import com.yugabyte.yw.common.TableSpaceStructures.TableSpaceQueryResponse;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.forms.CreateTablespaceParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.PlacementInfo.PlacementAZ;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Objects;
import java.util.Set;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class TableSpaceUtil {

  public static final String REPLICA_PLACEMENT_TEXT = "replica_placement=";

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

  private static void validateTablespace(TableSpaceInfo tsInfo, Universe universe) {
    PlacementInfo primaryClusterPlacement =
        universe.getUniverseDetails().getPrimaryCluster().placementInfo;

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
      validatePlacement(primaryClusterPlacement, pb);

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
