// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.core.type.TypeReference;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.commissioner.tasks.params.ServerSubTaskParams;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.TableSpaceStructures;
import com.yugabyte.yw.common.TableSpaceUtil;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.CustomerConfKeys;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.nodeui.DumpEntitiesResponse;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.TimeUnit;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import play.libs.Json;

@Slf4j
public class TablespaceValidationOnRemove extends UniverseTaskBase {
  public static final long TABLESPACE_READ_RETRY_TIMEOUT = TimeUnit.SECONDS.toMillis(10);
  public static final long TABLESPACE_READ_RETRIES = 5;

  @Inject
  protected TablespaceValidationOnRemove(BaseTaskDependencies baskTaskDependencies) {
    super(baskTaskDependencies);
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  public static class Params extends ServerSubTaskParams {
    public UUID clusterUUID;
    public PlacementInfo targetPlacement;
    public List<UniverseDefinitionTaskParams.PartitionInfo> targetPartitions;
  }

  @Override
  public void run() {
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    boolean cloudEnabled =
        confGetter.getConfForScope(
            Customer.get(universe.getCustomerId()), CustomerConfKeys.cloudEnabled);
    UniverseDefinitionTaskParams.Cluster cluster = universe.getCluster(taskParams().clusterUUID);
    if (!cluster.userIntent.enableYSQL) {
      log.warn("YSQL is not enabled, ignoring");
      return;
    }
    Set<UUID> removedZoneUUIDs = new HashSet<>();
    Map<UUID, UniverseDefinitionTaskParams.PartitionInfo> newGeoPartitionsMap =
        taskParams().targetPartitions.stream().collect(Collectors.toMap(g -> g.getUuid(), g -> g));
    Set<UUID> fullyRemovedZones = new HashSet<>();
    for (UniverseDefinitionTaskParams.PartitionInfo geoPartition : cluster.getPartitions()) {
      UniverseDefinitionTaskParams.PartitionInfo newPartition =
          newGeoPartitionsMap.get(geoPartition.getUuid());
      Set<UUID> r = getRemovedZones(newPartition, geoPartition);
      removedZoneUUIDs.addAll(r);
      if (newPartition == null) {
        fullyRemovedZones.addAll(r);
      }
    }
    if (removedZoneUUIDs.isEmpty()) {
      log.debug("No removed zones, skipping");
      return;
    }

    Set<String> zoneCodes =
        removedZoneUUIDs.stream()
            .map(z -> AvailabilityZone.getOrBadRequest(z))
            .map(az -> az.getCode())
            .collect(Collectors.toSet());

    Set<String> fullyRemovedZoneCodes =
        fullyRemovedZones.stream()
            .map(z -> AvailabilityZone.getOrBadRequest(z))
            .map(az -> az.getCode())
            .collect(Collectors.toSet());

    log.debug("Removed zones: {}", zoneCodes);

    Map<String, TableSpaceStructures.TableSpaceInfo> currentTablespaces = new HashMap<>();
    doWithConstTimeout(
        TABLESPACE_READ_RETRY_TIMEOUT,
        TABLESPACE_READ_RETRIES * TABLESPACE_READ_RETRY_TIMEOUT,
        () -> {
          NodeDetails randomTserver = CommonUtils.getARandomLiveTServer(universe);
          currentTablespaces.putAll(
              TableSpaceUtil.getCurrentTablespaces(randomTserver, universe, nodeUniverseManager));
        });

    Set<HostAndPort> tserverIps =
        universe.getUniverseDetails().nodeDetailsSet.stream()
            .filter(n -> removedZoneUUIDs.contains(n.azUuid))
            .map(
                n -> {
                  String ip = Util.getIpToUse(universe, n, cloudEnabled);
                  if (ip == null) {
                    return null;
                  }
                  return HostAndPort.fromParts(ip, n.tserverRpcPort);
                })
            .filter(Objects::nonNull)
            .collect(Collectors.toSet());

    DumpEntitiesResponse dumpEntitiesResponse = dumpDbEntities(universe, null);
    Set<DumpEntitiesResponse.Table> tablesOnRemoved =
        dumpEntitiesResponse.getTablesByTserverAddresses(tserverIps.toArray(new HostAndPort[0]));

    if (!tablesOnRemoved.isEmpty()) {
      List<String> tablesWithTablespaces = new ArrayList<>();
      doWithConstTimeout(
          TABLESPACE_READ_RETRY_TIMEOUT,
          TABLESPACE_READ_RETRIES * TABLESPACE_READ_RETRY_TIMEOUT,
          () -> {
            NodeDetails randomTserver = CommonUtils.getARandomLiveTServer(universe);
            ShellResponse shellResponse =
                nodeUniverseManager
                    .runYsqlCommand(
                        randomTserver,
                        universe,
                        "yugabyte",
                        TableSpaceUtil.FETCH_TABLES_WITH_TABLESPACES_QUERY)
                    .processErrors();

            String jsonData = CommonUtils.extractJsonisedSqlResponse(shellResponse);
            if (jsonData != null && !jsonData.isEmpty()) {
              ObjectMapper objectMapper = Json.mapper();
              Map<String, Set<String>> tablesByOID = new HashMap<>();
              try {
                List<ListTablesWithTablespacesResponse> tablespaceList =
                    objectMapper.readValue(
                        jsonData, new TypeReference<List<ListTablesWithTablespacesResponse>>() {});
                for (ListTablesWithTablespacesResponse r : tablespaceList) {
                  tablesByOID.computeIfAbsent(r.relfilenode, x -> new HashSet<>()).add(r.relname);
                }
                for (DumpEntitiesResponse.Table table : tablesOnRemoved) {
                  String last4 = table.getTableId().substring(table.getTableId().length() - 4);
                  String oid = String.valueOf(Integer.parseInt(last4, 16));
                  Set<String> tables = tablesByOID.getOrDefault(oid, new HashSet<>());
                  if (tables.contains(table.getTableName())) {
                    tablesWithTablespaces.add(table.getTableName());
                  }
                }
              } catch (JsonProcessingException e) {
                throw new RuntimeException("Failed to parse json response", e);
              }
            }
          });
      if (!tablesWithTablespaces.isEmpty()) {
        throw new RuntimeException(
            "These tables: "
                + tablesWithTablespaces
                + " have tablets on removed AZs ( "
                + zoneCodes
                + ") and cannot be moved");
      }
    }
    // This allows to run even though it will affect tablespaces because
    // these tablespaces are empty.
    if (!confGetter.getConfForScope(universe, UniverseConfKeys.checkTablespacesBeforeEdit)) {
      log.debug("Skipping tablespace check (disabled by config)");
      return;
    }
    Set<String> failedTablespaces = new HashSet<>();
    currentTablespaces.forEach(
        (name, info) -> {
          if (info.placementBlocks == null) {
            return;
          }
          boolean isFullyRemoved =
              info.placementBlocks.stream().allMatch(pb -> fullyRemovedZoneCodes.contains(pb.zone));
          boolean hasRemovedZone =
              info.placementBlocks.stream()
                  .filter(pb -> zoneCodes.contains(pb.zone))
                  .findFirst()
                  .isPresent();
          // For fully removed tablespaces we already checked that there are no tablets.
          if (!isFullyRemoved && hasRemovedZone) {
            failedTablespaces.add(name);
          }
        });
    if (!failedTablespaces.isEmpty()) {
      throw new RuntimeException(
          "Cannot remove zones "
              + zoneCodes
              + " because this will affect tablespaces "
              + failedTablespaces
              + ". Please modify tablespaces accordingly before submitting edit operation.");
    }
  }

  private static class ListTablesWithTablespacesResponse {
    public String relfilenode;
    public String relname;
    public String spcname;
  }

  private Set<UUID> getRemovedZones(
      UniverseDefinitionTaskParams.PartitionInfo newPartition,
      UniverseDefinitionTaskParams.PartitionInfo prevPartition) {
    Set<UUID> oldZones =
        prevPartition.getPlacement().azStream().map(az -> az.uuid).collect(Collectors.toSet());
    if (newPartition != null) {
      newPartition.getPlacement().azStream().forEach(az -> oldZones.remove(az.uuid));
    }
    return oldZones;
  }
}
