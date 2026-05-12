// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.google.api.client.util.Throwables;
import com.google.common.collect.ArrayListMultimap;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.TableSpaceStructures;
import com.yugabyte.yw.common.TableSpaceUtil;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.nodeui.DumpEntitiesResponse;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.time.Duration;
import java.util.ArrayList;
import java.util.Collection;
import java.util.HashSet;
import java.util.HexFormat;
import java.util.List;
import java.util.Map;
import java.util.Random;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.YBClient;
import play.libs.Json;

/**
 * Subtask to move tables from old to a new tablespace (in case of edit partition). There are 2
 * possible variants for that move: 1) With temporary tablespace (while keeping current tablespace
 * name) X->tmp->X1 2) With tablespace rename X->Y
 */
@Slf4j
public class MoveTablesTask extends BaseTablespacesTask {
  private static final int RETRIES = 5;
  private static final int SLEEP_BETWEEN_RETRIES = 30;

  private static final String SQL_ALL = "ALTER TABLE ALL IN TABLESPACE %s SET TABLESPACE %s";
  private static final String SQL = "ALTER TABLE %s SET TABLESPACE %s";

  @Inject
  protected MoveTablesTask(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends UniverseTaskParams {
    public Collection<UniverseDefinitionTaskParams.PartitionInfo> partitionInfos;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  enum Step {
    MOVE_TO_TEMP,
    MOVE_TO_NEW
  }

  @Override
  public void run() {
    log.info("Running {}", getName());
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    List<UniverseDefinitionTaskParams.PartitionInfo> partitionInfos =
        new ArrayList<>(taskParams().partitionInfos);
    TablesAndTablespacesInfo tablesAndTablespaces =
        getTablesAndTablespaces(
            universe, universe.getNodes(), () -> dumpDbEntities(universe, null));

    Map<UUID, UniverseDefinitionTaskParams.PartitionInfo> oldPartitions =
        universe.getUniverseDetails().clusters.stream()
            .flatMap(c -> c.getPartitions().stream())
            .collect(Collectors.toMap(p -> p.getUuid(), p -> p));

    ArrayListMultimap<UUID, Step> multimapByStep = ArrayListMultimap.create();

    for (UniverseDefinitionTaskParams.PartitionInfo partitionInfo : partitionInfos) {
      UniverseDefinitionTaskParams.PartitionInfo oldPartition =
          oldPartitions.get(partitionInfo.getUuid());

      TableSpaceStructures.TableSpaceInfo oldState =
          tablesAndTablespaces.tableSpaceInfoMap.get(oldPartition.getTablespaceName());
      TableSpaceStructures.TableSpaceInfo oldSchema =
          TableSpaceUtil.partitionToTablespace(oldPartition);

      if (shouldCreateTemp(oldPartition, partitionInfo)) {
        // Verifying that the retry will continue from the same step.
        // Possible stages of failing of prev task:
        // 1) Nothing changed
        // 2) Tmp tablespace created
        // 3) Tables are moved
        // 4) Original tablespace is dropped
        // 5) New tablespace is created
        // 6) Tables are moved to new tablespace
        // 7) Tmp tablespace is dropped
        // For 1-3 we will retry from the beginning (oldState should be equal to oldSchema)
        // For 4-6 we will skip MOVE_TO_TEMP and proceed with MOVE_TO_NEW
        // (oldState either null or not equal to oldSchema)
        if (oldState != null && oldState.equals(oldSchema)) {
          multimapByStep.put(partitionInfo.getUuid(), Step.MOVE_TO_TEMP);
        }
      }
      multimapByStep.put(partitionInfo.getUuid(), Step.MOVE_TO_NEW);
    }

    int attempt = 0;
    boolean alterAllSupported = isAlterAllSupported(universe);
    while (!multimapByStep.isEmpty() && attempt++ < RETRIES) {
      try {
        tablesAndTablespaces =
            getTablesAndTablespaces(
                universe, universe.getNodes(), () -> dumpDbEntities(universe, null));
        for (int i = 0; i < Step.values().length; i++) {
          for (UniverseDefinitionTaskParams.PartitionInfo partitionInfo : partitionInfos) {
            List<Step> stepsForPartition = multimapByStep.get(partitionInfo.getUuid());
            log.debug(
                "Steps for tablespace {} are {}",
                partitionInfo.getTablespaceName(),
                stepsForPartition);
            if (stepsForPartition == null || stepsForPartition.isEmpty()) {
              continue;
            }
            UniverseDefinitionTaskParams.PartitionInfo oldPartition =
                oldPartitions.get(partitionInfo.getUuid());
            NodeDetails randomTserver = pickRandomTserver(universe, partitionInfo, oldPartition);
            Step step = stepsForPartition.iterator().next();
            Pair<String, String> names = getOriginAndTarget(step, partitionInfo, oldPartition);

            createTablespaceIfNotExists(randomTserver, universe, names.getSecond(), partitionInfo);
            moveTables(
                randomTserver,
                universe,
                names,
                tablesAndTablespaces.getTableToTablespace(),
                alterAllSupported);
          }
          // Wait till load balancer finishes refreshing tablespace info.
          waitFor(Duration.ofSeconds(90));
          waitForLoadBalance(universe);
          for (UniverseDefinitionTaskParams.PartitionInfo partitionInfo : partitionInfos) {
            List<Step> stepsForPartition = multimapByStep.get(partitionInfo.getUuid());
            if (stepsForPartition == null || stepsForPartition.isEmpty()) {
              continue;
            }
            UniverseDefinitionTaskParams.PartitionInfo oldPartition =
                oldPartitions.get(partitionInfo.getUuid());
            NodeDetails randomTserver = pickRandomTserver(universe, partitionInfo, oldPartition);
            Step step = stepsForPartition.iterator().next();
            Pair<String, String> names = getOriginAndTarget(step, partitionInfo, oldPartition);

            String query = String.format(DropTablespacesTask.SQL, names.getFirst());
            nodeUniverseManager
                .runYsqlCommand(randomTserver, universe, TableSpaceUtil.DB, query)
                .processErrors();

            stepsForPartition.remove(step);
          }
        }

      } catch (Exception e) {
        log.error("Failed to move tables", e);
        waitFor(Duration.ofSeconds(SLEEP_BETWEEN_RETRIES));
      }
    }
    if (!multimapByStep.isEmpty()) {
      throw new RuntimeException(
          "Failed to move tables for partitions: " + multimapByStep.keySet());
    }
  }

  private NodeDetails pickRandomTserver(
      Universe universe,
      UniverseDefinitionTaskParams.PartitionInfo partitionInfo,
      UniverseDefinitionTaskParams.PartitionInfo oldPartition) {
    Set<UUID> azs = new HashSet<>(partitionInfo.getPlacement().getAllAZUUIDs());
    azs.addAll(oldPartition.getPlacement().getAllAZUUIDs());
    // We can pick any tserver since they all should be live at that moment.
    List<NodeDetails> nodes =
        universe
            .getUniverseDetails()
            .getNodesInCluster(universe.getUniverseDetails().getPrimaryCluster().uuid)
            .stream()
            .filter(n -> azs.contains(n.azUuid))
            .filter(n -> n.state != NodeDetails.NodeState.ToBeRemoved)
            .collect(Collectors.toList());
    if (nodes.isEmpty()) {
      throw new IllegalStateException(
          "No available nodes for partition " + partitionInfo.getName());
    }
    return nodes.get(new Random().nextInt(nodes.size()));
  }

  private void waitForLoadBalance(Universe universe) {
    try (YBClient client = ybService.getUniverseClient(universe)) {
      if (!client.waitForLoadBalance(Integer.MAX_VALUE, 0)) {
        throw new IllegalStateException("Failed to wait for load balance");
      }
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      Throwables.propagate(e);
    }
  }

  private Pair<String, String> getOriginAndTarget(
      Step step,
      UniverseDefinitionTaskParams.PartitionInfo partitionInfo,
      UniverseDefinitionTaskParams.PartitionInfo oldPartition) {
    String tmpName = getTemporaryTablespaceName(partitionInfo, oldPartition);
    String targetTablespaceName =
        step == Step.MOVE_TO_TEMP ? tmpName : partitionInfo.getTablespaceName();
    String currentTablespaceName = oldPartition.getTablespaceName();
    if (shouldCreateTemp(oldPartition, partitionInfo) && step == Step.MOVE_TO_NEW) {
      currentTablespaceName = tmpName;
    }
    return new Pair<>(currentTablespaceName, targetTablespaceName);
  }

  private void moveTables(
      NodeDetails randomTserver,
      Universe universe,
      Pair<String, String> names,
      Map<DumpEntitiesResponse.Table, String> tableToTablespace,
      boolean alterAllSupported) {
    String from = names.getFirst();
    String to = names.getSecond();
    log.debug("Table to tablespace {}", tableToTablespace);
    if (alterAllSupported) {
      String query = String.format(SQL_ALL, from, to);

      nodeUniverseManager
          .runYsqlCommand(randomTserver, universe, Util.YUGABYTE_DB, query)
          .processErrors();
    } else {
      Set<String> tables =
          tableToTablespace.entrySet().stream()
              .filter(e -> e.getValue().equals(from))
              .map(e -> e.getKey().getTableName())
              .collect(Collectors.toSet());

      for (String table : tables) {
        log.debug("Moving table {} from {} to {}", table, from, to);
        String query = String.format(SQL, table, to);

        nodeUniverseManager
            .runYsqlCommand(randomTserver, universe, Util.YUGABYTE_DB, query)
            .processErrors();
      }
    }
  }

  private void createTablespaceIfNotExists(
      NodeDetails nodeDetails,
      Universe universe,
      String targetTablespaceName,
      UniverseDefinitionTaskParams.PartitionInfo partitionInfo) {
    Map<String, TableSpaceStructures.TableSpaceInfo> currentTablespaces =
        TableSpaceUtil.getCurrentTablespaces(nodeDetails, universe, nodeUniverseManager);
    TableSpaceStructures.TableSpaceInfo tableSpaceInfo =
        TableSpaceUtil.partitionToTablespace(partitionInfo);
    tableSpaceInfo.name = targetTablespaceName;
    TableSpaceStructures.TableSpaceInfo currentTablesSpaceInfo =
        currentTablespaces.get(targetTablespaceName);
    if (currentTablesSpaceInfo != null && !currentTablesSpaceInfo.equals(tableSpaceInfo)) {
      throw new IllegalStateException(
          "Expected to create tablespace "
              + tableSpaceInfo
              + " but found "
              + currentTablesSpaceInfo);
    }
    String createTablespaceQuery = CreateTableSpaces.getTablespaceCreationQuery(tableSpaceInfo);
    ShellResponse response =
        nodeUniverseManager
            .runYsqlCommand(nodeDetails, universe, TableSpaceUtil.DB, createTablespaceQuery)
            .processErrors();
    log.debug("Tablespace creation response is {}", response.message);
  }

  private boolean shouldCreateTemp(
      UniverseDefinitionTaskParams.PartitionInfo oldPartition,
      UniverseDefinitionTaskParams.PartitionInfo partitionInfo) {
    return oldPartition.getTablespaceName().equals(partitionInfo.getTablespaceName());
  }

  private boolean isAlterAllSupported(Universe universe) {
    String dbVersion =
        universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
    return CommonUtils.isReleaseEqualOrAfter("2025.1.0.0", dbVersion);
  }

  private String getTemporaryTablespaceName(
      UniverseDefinitionTaskParams.PartitionInfo newPartition,
      UniverseDefinitionTaskParams.PartitionInfo oldPartition) {
    String suffix;
    try {
      // Taking hash of the target placement. Should be the same for retry.
      MessageDigest md = MessageDigest.getInstance("MD5");
      byte[] digest = md.digest(Json.toJson(newPartition.getPlacement()).toString().getBytes());
      suffix = HexFormat.of().formatHex(digest).substring(0, 8);
    } catch (NoSuchAlgorithmException e) {
      suffix = "partition";
    }
    return newPartition.getTablespaceName() + "_edit_" + suffix;
  }
}
