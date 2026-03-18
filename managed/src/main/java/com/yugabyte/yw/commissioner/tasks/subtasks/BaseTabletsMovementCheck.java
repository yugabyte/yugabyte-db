// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.google.common.collect.ArrayListMultimap;
import com.google.common.collect.Multimap;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.TableSpaceStructures;
import com.yugabyte.yw.common.TableSpaceUtil;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.nodeui.DumpEntitiesResponse;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import java.util.ArrayList;
import java.util.Collection;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;
import java.util.function.Supplier;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public abstract class BaseTabletsMovementCheck extends BaseTablespacesTask {

  @Inject
  protected BaseTabletsMovementCheck(BaseTaskDependencies baskTaskDependencies) {
    super(baskTaskDependencies);
  }

  protected void checkNodesRemoval(
      Collection<NodeDetails> removedNodes,
      Universe universe,
      PlacementInfo targetPlacementInfo,
      Set<String> tablespacesToIgnore,
      boolean nodeRemoval) {
    Set<UUID> clusterUUIDs =
        removedNodes.stream().map(n -> n.placementUuid).collect(Collectors.toSet());
    if (clusterUUIDs.size() > 1) {
      throw new IllegalArgumentException(
          "Expected to have a single cluster, found " + clusterUUIDs);
    }

    UniverseDefinitionTaskParams.Cluster cluster =
        universe.getCluster(clusterUUIDs.iterator().next());

    int timeoutSec = confGetter.getGlobalConf(GlobalConfKeys.tabletsMovementVerificationTimeoutSec);
    boolean allowBrokenTablespaces =
        confGetter.getGlobalConf(GlobalConfKeys.allowIncorrectTablespaces);

    Map<String, AvailabilityZone> targetZones = new HashMap<>();
    targetPlacementInfo
        .azStream()
        .forEach(
            az -> {
              if (az.numNodesInAZ > 0) {
                AvailabilityZone zone = AvailabilityZone.getOrBadRequest(az.uuid);
                targetZones.put(zone.getCode(), zone);
              }
            });

    Set<String> errors = new HashSet<>();

    Multimap<UUID, NodeDetails> nodesByZone = ArrayListMultimap.create();
    removedNodes.forEach(n -> nodesByZone.put(n.azUuid, n));

    doWithExponentialTimeout(
        TimeUnit.SECONDS.toMillis(4),
        TimeUnit.SECONDS.toMillis(30),
        TimeUnit.SECONDS.toMillis(timeoutSec),
        () -> {
          errors.clear();
          TablesAndTablespacesInfo tablesAndTablespaces = null;

          // Lazy loading.
          Supplier<DumpEntitiesResponse> dumpEntitiesResponseSupplier =
              getDumpEntitesSupplier(universe);

          if (cluster.userIntent.enableYSQL && cluster.isGeoPartitioned()) {
            tablesAndTablespaces =
                getTablesAndTablespaces(universe, removedNodes, dumpEntitiesResponseSupplier);
            log.debug("Ignoring tablespaces {}", tablespacesToIgnore);
            tablesAndTablespaces.tableSpaceInfoMap.keySet().removeAll(tablespacesToIgnore);
          }

          for (Map.Entry<UUID, Collection<NodeDetails>> entry : nodesByZone.asMap().entrySet()) {
            UUID azUUID = entry.getKey();
            AvailabilityZone availabilityZone = AvailabilityZone.getOrBadRequest(azUUID);
            Collection<NodeDetails> nodes = entry.getValue();
            NodeDetails sampleNode = nodes.iterator().next(); // All the nodes are in the same az.

            // This logic depends on which operation is it:
            // 1) For edit-like operation, we just count according to the target placement.
            // 2) For remove node operation we exclude current node from the result and check the
            // actual availability of servers.
            int tserversToMoveTo =
                getAvailableTserversPerZone(sampleNode, universe, targetPlacementInfo);
            int rfInZone = getRFInZone(cluster, azUUID);

            log.debug(
                "Zone {} tservers to move to {}, rf in zone {}",
                azUUID,
                tserversToMoveTo,
                rfInZone);

            if (tablesAndTablespaces != null) {
              Map<String, TableSpaceStructures.TableSpaceInfo> tablespaceByName =
                  tablesAndTablespaces.getTableSpaceInfoMap();

              tablesAndTablespaces.getTableToTablespace().values().stream()
                  .distinct()
                  .forEach(
                      tablespaceName -> {
                        if (tablespacesToIgnore.contains(tablespaceName)) {
                          return;
                        }
                        int replicas =
                            getRFInTablespace(
                                tablespaceByName.get(tablespaceName), sampleNode, rfInZone);
                        if (replicas > tserversToMoveTo) {
                          String error;
                          if (tserversToMoveTo == 0) {
                            error =
                                String.format(
                                    "Non-empty tablespace %s contains zone %s, which will be"
                                        + " removed by the operation.",
                                    tablespaceName, availabilityZone.getName());
                          } else {
                            error =
                                String.format(
                                    "Non-empty tablespace %s has %d replicas in zone %s, but there"
                                        + " will be only %d nodes after operation.",
                                    tablespaceName,
                                    replicas,
                                    availabilityZone.getName(),
                                    tserversToMoveTo);
                          }
                          errors.add(error);
                        }
                      });
              List<TableSpaceStructures.TableSpaceInfo> tablespacesForNodes =
                  tablespaceByName.values().stream()
                      .filter(
                          t -> TableSpaceUtil.findPlacementBlockByNode(t, sampleNode).isPresent())
                      .collect(Collectors.toList());
              if (tserversToMoveTo == 0 && !allowBrokenTablespaces) {
                List<String> brokenTablespaces = new ArrayList<>();
                for (TableSpaceStructures.TableSpaceInfo tablespacesForNode : tablespacesForNodes) {
                  if (tablesAndTablespaces.hasTables(tablespacesForNode.name)
                      || tablespacesToIgnore.contains(tablespacesForNode.name)) {
                    continue; // Already checked.
                  }
                  // Allow completely removing tablespaces (e.g. for geo removal).
                  if (!isFullyRemoved(tablespacesForNode, targetZones)) {
                    brokenTablespaces.add(tablespacesForNode.name);
                  }
                }
                if (!brokenTablespaces.isEmpty()) {
                  log.error(
                      "Found tablespaces that will be incorrect after operation: {}",
                      brokenTablespaces);
                  String error =
                      String.format(
                          "Empty tablespaces %s contain zone %s, which will be removed by the"
                              + " operation",
                          brokenTablespaces, availabilityZone.getName());
                  errors.add(
                      addCheckName(error, GlobalConfKeys.allowIncorrectTablespaces.getKey()));
                }
              }
            }
            // For other cases like edit universe we don't allow that at a high level prechecks.
            if (nodeRemoval && rfInZone > tserversToMoveTo) {
              // Non-geo partitioned tables that have nowhere to move.
              // We need to verify that there are no tablets on nodes.
              Set<String> tables =
                  getTablesOnNodes(dumpEntitiesResponseSupplier.get(), universe, nodes).stream()
                      .map(DumpEntitiesResponse.Table::getTableName)
                      .collect(Collectors.toSet());
              if (tables.size() > 0) {
                String error =
                    String.format(
                        "Zone %s has %d replicas, but only %d available tservers found."
                            + " Expected to have 0 tablets on the node to remove it.",
                        availabilityZone.getName(), rfInZone, tserversToMoveTo);
                errors.add(addCheckName(error, UniverseConfKeys.alwaysWaitForDataMove.getKey()));
              }
            }
          }
          return errors.isEmpty();
        });
    if (!errors.isEmpty()) {
      throw new RuntimeException(getErrorMessage() + ", found errors: " + errors);
    }
  }

  private String addCheckName(String errorMessage, String configName) {
    return errorMessage + "This check could be disabled by " + configName + " runtime config.";
  }

  private boolean isFullyRemoved(
      TableSpaceStructures.TableSpaceInfo tablespacesForNode,
      Map<String, AvailabilityZone> targetZones) {
    return tablespacesForNode.placementBlocks.stream()
        .allMatch(
            block -> {
              AvailabilityZone az = targetZones.get(block.zone);
              return az == null || TableSpaceUtil.isSameAz(block, az);
            });
  }

  private Supplier<DumpEntitiesResponse> getDumpEntitesSupplier(Universe universe) {
    AtomicReference<DumpEntitiesResponse> ref = new AtomicReference<>();
    return () -> {
      if (ref.get() != null) {
        return ref.get();
      }
      ref.set(dumpDbEntities(universe, null));
      return ref.get();
    };
  }

  protected abstract String getErrorMessage();

  private int getRFInTablespace(
      TableSpaceStructures.TableSpaceInfo tableSpaceInfo, NodeDetails currentNode, int orElseRF) {
    if (tableSpaceInfo == null) {
      return orElseRF;
    }
    return TableSpaceUtil.findPlacementBlockByNode(tableSpaceInfo, currentNode)
        .map(pb -> pb.minNumReplicas)
        .orElse(orElseRF);
  }

  protected int getRFInZone(UniverseDefinitionTaskParams.Cluster currCluster, UUID azUUID) {
    PlacementInfo pi = currCluster.getOverallPlacement();
    int rf =
        pi.azStream()
            .filter(az -> az.uuid.equals(azUUID))
            .map(az -> az.replicationFactor)
            .findFirst()
            .orElse(-1);
    if (rf < 0) {
      throw new IllegalStateException("Zone  " + azUUID + " is not found in placement");
    }
    return rf;
  }

  protected int getAvailableTserversPerZone(
      NodeDetails currentNode, Universe universe, PlacementInfo targetPlacement) {
    PlacementInfo.PlacementAZ placementAZ = targetPlacement.findByAZUUID(currentNode.azUuid);
    return placementAZ == null ? 0 : placementAZ.numNodesInAZ;
  }
}
