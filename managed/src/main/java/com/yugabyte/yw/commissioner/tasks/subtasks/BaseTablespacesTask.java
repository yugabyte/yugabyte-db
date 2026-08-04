// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.core.type.TypeReference;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.common.collect.ArrayListMultimap;
import com.google.common.collect.Multimap;
import com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.TableSpaceStructures;
import com.yugabyte.yw.common.TableSpaceUtil;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.CustomerConfKeys;
import com.yugabyte.yw.common.nodeui.DumpEntitiesResponse;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.Collection;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Random;
import java.util.Set;
import java.util.concurrent.TimeUnit;
import java.util.function.Predicate;
import java.util.function.Supplier;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.Data;
import lombok.extern.slf4j.Slf4j;
import play.libs.Json;

@Slf4j
public abstract class BaseTablespacesTask extends UniverseTaskBase {
  public static final long TABLESPACE_READ_RETRY_TIMEOUT = TimeUnit.SECONDS.toMillis(10);
  public static final long TABLESPACE_READ_RETRIES = 5;

  @Inject
  protected BaseTablespacesTask(BaseTaskDependencies baskTaskDependencies) {
    super(baskTaskDependencies);
  }

  protected Set<DumpEntitiesResponse.Table> getTablesOnNodes(
      DumpEntitiesResponse dumpEntitiesResponse,
      Universe universe,
      Collection<NodeDetails> removedNodes) {
    boolean cloudEnabled =
        confGetter.getConfForScope(
            Customer.get(universe.getCustomerId()), CustomerConfKeys.cloudEnabled);
    Set<HostAndPort> tserverIps =
        removedNodes.stream()
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
    return dumpEntitiesResponse.getTablesByTserverAddresses(tserverIps.toArray(new HostAndPort[0]));
  }

  protected Map<String, TableSpaceStructures.TableSpaceInfo> getTablespaces(
      Universe universe, Predicate<NodeDetails> nodeFilter) {
    Map<String, TableSpaceStructures.TableSpaceInfo> tablespaces = new HashMap<>();
    List<NodeDetails> nodes =
        universe.getNodesInCluster(universe.getUniverseDetails().getPrimaryCluster().uuid).stream()
            .filter(n -> n.isTserver)
            .filter(
                n -> {
                  if (nodeFilter != null) {
                    return nodeFilter.test(n);
                  }
                  return n.state == NodeDetails.NodeState.Live;
                })
            .collect(Collectors.toList());
    if (nodes.isEmpty()) {
      log.error(
          "Failed to find any tserver node {}",
          nodeFilter == null ? "with Live status" : "matching filter");
      throw new IllegalStateException("No available node to fetch tablespace from");
    }
    doWithConstTimeout(
        TABLESPACE_READ_RETRY_TIMEOUT,
        TABLESPACE_READ_RETRIES * TABLESPACE_READ_RETRY_TIMEOUT,
        () -> {
          NodeDetails randomTserver = nodes.get(new Random().nextInt(nodes.size()));
          tablespaces.putAll(
              TableSpaceUtil.getCurrentTablespaces(randomTserver, universe, nodeUniverseManager));
        });
    // Removing built-in tablespaces like `pg_global`, `pg_default`.
    tablespaces.entrySet().removeIf(e -> e.getValue().numReplicas == 0);
    return tablespaces;
  }

  /**
   * Tablespaces and table to tablespace mapping for current nodes. Returns empty result in case of
   * the absence of tablespaces.
   *
   * @param universe
   * @param nodes
   * @return
   */
  protected TablesAndTablespacesInfo getTablesAndTablespaces(
      Universe universe,
      Collection<NodeDetails> nodes,
      Supplier<DumpEntitiesResponse> dumpEntitiesResponseSupplier) {
    Map<String, TableSpaceStructures.TableSpaceInfo> tablespaces =
        getTablespaces(universe, n -> n.state == NodeDetails.NodeState.Live);
    Map<DumpEntitiesResponse.Table, String> tableToTablespace = new HashMap<>();
    if (tablespaces.isEmpty()) {
      log.debug("Zero tablespaces found, returning empty result");
      return new TablesAndTablespacesInfo(tablespaces, tableToTablespace);
    }

    Set<DumpEntitiesResponse.Table> tablesOnNodes =
        getTablesOnNodes(dumpEntitiesResponseSupplier.get(), universe, nodes);
    if (!tablesOnNodes.isEmpty()) {
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
            if (!jsonData.isEmpty()) {
              ObjectMapper objectMapper = Json.mapper();
              try {
                List<ListTablesWithTablespacesResponse> tablespaceList =
                    objectMapper.readValue(
                        jsonData, new TypeReference<List<ListTablesWithTablespacesResponse>>() {});
                Multimap<String, DumpEntitiesResponse.Table> tablesByOID =
                    ArrayListMultimap.create();
                /*
                The last four chars of the UUID are the OID of the table in hex. For ex:
                UUID: 000033c3000030008000000000004000
                OID in hex: 0x4000
                OID in decimal: 16384*/
                for (DumpEntitiesResponse.Table table : tablesOnNodes) {
                  String last4 = table.getTableId().substring(table.getTableId().length() - 4);
                  String oid = String.valueOf(Integer.parseInt(last4, 16));
                  tablesByOID.put(oid, table);
                }
                for (ListTablesWithTablespacesResponse r : tablespaceList) {
                  // relfilenode is referencing table by OID.
                  Collection<DumpEntitiesResponse.Table> tables = tablesByOID.get(r.relfilenode);
                  for (DumpEntitiesResponse.Table table : tables) {
                    // relname should be table name.
                    if (table.getTableName().equals(r.relname)) {
                      tableToTablespace.put(table, r.spcname);
                      break;
                    }
                  }
                }

              } catch (JsonProcessingException e) {
                throw new RuntimeException("Failed to parse json response", e);
              }
            }
          });
    }
    return new TablesAndTablespacesInfo(tablespaces, tableToTablespace);
  }

  protected NodeDetails getRandomTserver(Universe universe) {
    List<NodeDetails> nodes =
        universe
            .getUniverseDetails()
            .getNodesInCluster(universe.getUniverseDetails().getPrimaryCluster().uuid)
            .stream()
            .filter(n -> n.state != NodeDetails.NodeState.ToBeRemoved)
            .collect(Collectors.toList());
    if (nodes.isEmpty()) {
      throw new IllegalStateException("No available nodes to run drop tablespaces");
    }
    return nodes.get(new Random().nextInt(nodes.size()));
  }

  @Data
  protected static class TablesAndTablespacesInfo {
    final Map<String, TableSpaceStructures.TableSpaceInfo> tableSpaceInfoMap;
    final Map<DumpEntitiesResponse.Table, String> tableToTablespace;

    public boolean hasTables(String tablespaceName) {
      return tableToTablespace.values().contains(tablespaceName);
    }
  }

  private static class ListTablesWithTablespacesResponse {
    public String relfilenode;
    public String relname;
    public String spcname;
  }
}
