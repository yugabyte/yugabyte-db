// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.commissioner.tasks.subtasks.xcluster;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.forms.XClusterConfigTaskParams;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterTableConfig;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.AlterUniverseReplicationResponse;
import org.yb.client.YBClient;
import org.yb.master.CatalogEntityInfo;

@Slf4j
public class XClusterConfigModifyTables extends XClusterConfigTaskBase {

  @Inject
  protected XClusterConfigModifyTables(
      BaseTaskDependencies baseTaskDependencies, XClusterUniverseService xClusterUniverseService) {
    super(baseTaskDependencies, xClusterUniverseService);
  }

  public static class Params extends XClusterConfigTaskParams {
    // The target universe UUID must be stored in universeUUID field.
    // The parent xCluster config must be stored in xClusterConfig field.
    // Tables to apply the action to.
    public Set<String> tables;

    public enum Action {
      // Add the tables to the replication group.
      ADD,
      // Delete the tables from the replication group and remove their corresponding DB entry.
      DELETE,
      // Remove the tables from the replication group but keep its DB entry.
      REMOVE_FROM_REPLICATION_ONLY;

      @Override
      public String toString() {
        return name().toLowerCase();
      }
    }

    public Action action;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public String getName() {
    return String.format(
        "%s(targetUniverse=%s,xClusterConfig=%s,tables=%s,action=%s)",
        super.getName(),
        taskParams().getUniverseUUID(),
        taskParams().getXClusterConfig(),
        taskParams().tables,
        taskParams().action);
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    XClusterConfig xClusterConfig = getXClusterConfigFromTaskParams();
    Map<String, String> tableIdsToAddBootstrapIdsMap = new HashMap<>();
    Set<String> tableIdsToAdd;
    Set<String> tableIdsToRemove;
    if (taskParams().action == Params.Action.ADD) {
      tableIdsToAdd = taskParams().tables;
      tableIdsToRemove = new HashSet<>();
    } else {
      tableIdsToAdd = new HashSet<>();
      tableIdsToRemove = taskParams().tables;
    }

    // Ensure each tableId exits in the xCluster config and replication is not set up for it. Also,
    // get the bootstrapIds if there is any.
    for (String tableId : tableIdsToAdd) {
      Optional<XClusterTableConfig> tableConfig = xClusterConfig.maybeGetTableById(tableId);
      if (!tableConfig.isPresent()) {
        String errMsg =
            String.format(
                "Table with id (%s) does not belong to the task params xCluster config (%s)",
                tableId, xClusterConfig.getUuid());
        throw new IllegalArgumentException(errMsg);
      }
      if (tableConfig.get().isReplicationSetupDone()) {
        String errMsg =
            String.format(
                "Replication is already set up for table with id (%s) and cannot be set up again",
                tableId);
        throw new IllegalArgumentException(errMsg);
      }
      tableIdsToAddBootstrapIdsMap.put(tableId, tableConfig.get().getStreamId());
    }
    // Either all tables should need bootstrap, or none should.
    if (tableIdsToAddBootstrapIdsMap.values().stream().anyMatch(Objects::isNull)
        && tableIdsToAddBootstrapIdsMap.values().stream().anyMatch(Objects::nonNull)) {
      throw new IllegalArgumentException(
          String.format(
              "Failed to modify XClusterConfig(%s) to add tables because some tables went "
                  + "through bootstrap and some did not, You must create modify tables subtasks "
                  + "separately for them",
              xClusterConfig.getUuid()));
    }

    // Ensure each tableId exits in the xCluster config.
    for (String tableId : tableIdsToRemove) {
      Optional<XClusterTableConfig> tableConfig = xClusterConfig.maybeGetTableById(tableId);
      if (!tableConfig.isPresent()) {
        String errMsg =
            String.format(
                "Table with id (%s) is not part of xCluster config (%s) and cannot be removed",
                tableId, xClusterConfig.getUuid());
        throw new IllegalArgumentException(errMsg);
      }
    }
    log.info("Modifying tables in XClusterConfig({})", xClusterConfig.getUuid());

    Universe targetUniverse = Universe.getOrBadRequest(xClusterConfig.getTargetUniverseUUID());
    String targetUniverseMasterAddresses = targetUniverse.getMasterAddresses();
    String targetUniverseCertificate = targetUniverse.getCertificateNodetoNode();
    try (YBClient client =
        ybService.getClient(targetUniverseMasterAddresses, targetUniverseCertificate)) {
      if (tableIdsToAddBootstrapIdsMap.size() > 0) {
        try {
          log.info(
              "Adding tables to XClusterConfig({}): tableIdsToAddBootstrapIdsMap {}",
              xClusterConfig.getUuid(),
              tableIdsToAddBootstrapIdsMap);
          AlterUniverseReplicationResponse resp =
              client.alterUniverseReplicationAddTables(
                  xClusterConfig.getReplicationGroupName(), tableIdsToAddBootstrapIdsMap);
          if (resp.hasError()) {
            String errMsg =
                String.format(
                    "Failed to add tables to XClusterConfig(%s): %s",
                    xClusterConfig.getUuid(), resp.errorMessage());
            throw new RuntimeException(errMsg);
          }
          waitForXClusterOperation(xClusterConfig, client::isAlterUniverseReplicationDone);

          // Get the stream ids from the target universe and put it in the Platform DB for the
          // added tables to the xCluster config.
          CatalogEntityInfo.SysClusterConfigEntryPB clusterConfig =
              getClusterConfig(client, targetUniverse.getUniverseUUID());
          syncXClusterConfigWithReplicationGroup(clusterConfig, xClusterConfig, tableIdsToAdd);

          if (HighAvailabilityConfig.get().isPresent()) {
            // Note: We increment version twice for adding tables: once for setting up the .ALTER
            // replication group, and once for merging the .ALTER replication group
            getUniverse(true).incrementVersion();
            getUniverse(true).incrementVersion();
          }
        } catch (Exception e) {
          xClusterConfig.updateStatusForTables(tableIdsToAdd, XClusterTableConfig.Status.Failed);
          throw e;
        }
      }

      if (tableIdsToRemove.size() > 0) {
        try {
          log.info(
              "Removing tables from XClusterConfig({}): {}",
              xClusterConfig.getUuid(),
              tableIdsToRemove);
          // Sync the state for the tables to be removed.
          CatalogEntityInfo.SysClusterConfigEntryPB clusterConfig =
              getClusterConfig(client, targetUniverse.getUniverseUUID());
          boolean replicationGroupExists =
              syncReplicationSetUpStateForTables(clusterConfig, xClusterConfig, tableIdsToRemove);
          if (!replicationGroupExists) {
            throw new RuntimeException(
                String.format(
                    "No replication group found with name (%s) in universe (%s) cluster config",
                    xClusterConfig.getReplicationGroupName(),
                    xClusterConfig.getTargetUniverseUUID()));
          }
          Set<String> tableIdsToRemoveWithReplication =
              xClusterConfig.getTableIdsWithReplicationSetup().stream()
                  .filter(tableIdsToRemove::contains)
                  .collect(Collectors.toSet());
          log.debug(
              "Table IDs to remove with replication set up: {}", tableIdsToRemoveWithReplication);

          // Removing all tables from a replication config is not allowed.
          if (tableIdsToRemoveWithReplication.size()
                  + xClusterConfig.getTableIdsWithReplicationSetup(false /* done */).size()
              == xClusterConfig.getTableIds().size()) {
            throw new RuntimeException(
                String.format(
                    "The operation to remove tables from replication config will remove all the "
                        + "tables in replication which is not allowed; if you want to delete "
                        + "replication for all of them, please delete the replication config: %s",
                    xClusterConfig));
          }

          // Remove the tables from the replication group if there is any.
          if (!tableIdsToRemoveWithReplication.isEmpty()) {
            AlterUniverseReplicationResponse resp =
                client.alterUniverseReplicationRemoveTables(
                    xClusterConfig.getReplicationGroupName(), tableIdsToRemoveWithReplication);
            if (resp.hasError()) {
              throw new RuntimeException(
                  String.format(
                      "Failed to remove tables from XClusterConfig(%s): %s",
                      xClusterConfig.getUuid(), resp.errorMessage()));
            }

            if (HighAvailabilityConfig.get().isPresent()) {
              getUniverse(true).incrementVersion();
            }
          }

          if (taskParams().action == Params.Action.DELETE) {
            xClusterConfig.removeTables(tableIdsToRemove);
          } else {
            xClusterConfig
                .getTablesById(tableIdsToRemove)
                .forEach(
                    tableConfig -> {
                      tableConfig.setStatus(XClusterTableConfig.Status.Validated);
                      tableConfig.setReplicationSetupDone(false);
                      tableConfig.setStreamId(null);
                      tableConfig.setBootstrapCreateTime(null);
                      tableConfig.setRestoreTime(null);
                    });
            xClusterConfig.update();
          }
        } catch (Exception e) {
          xClusterConfig.updateStatusForTables(tableIdsToRemove, XClusterTableConfig.Status.Failed);
          throw e;
        }
      }
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    }

    log.info("Completed {}", getName());
  }
}
