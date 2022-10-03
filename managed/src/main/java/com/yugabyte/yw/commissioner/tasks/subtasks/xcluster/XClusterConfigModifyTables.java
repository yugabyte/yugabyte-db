// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.commissioner.tasks.subtasks.xcluster;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.forms.XClusterConfigTaskParams;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterTableConfig;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.cdc.CdcConsumer;
import org.yb.client.AlterUniverseReplicationResponse;
import org.yb.client.YBClient;
import org.yb.master.CatalogEntityInfo;

@Slf4j
public class XClusterConfigModifyTables extends XClusterConfigTaskBase {

  @Inject
  protected XClusterConfigModifyTables(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends XClusterConfigTaskParams {
    // The target universe UUID must be stored in universeUUID field.
    // The parent xCluster config must be stored in xClusterConfig field.
    // Table ids to add to the replication.
    public Set<String> tableIdsToAdd;
    // Table ids to remove from the replication.
    public Set<String> tableIdsToRemove;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public String getName() {
    return String.format(
        "%s(targetUniverse=%s,xClusterConfig=%s,tableIdsToAdd=%s,tableIdsToRemove=%s)",
        super.getName(),
        taskParams().universeUUID,
        taskParams().xClusterConfig,
        taskParams().tableIdsToAdd,
        taskParams().tableIdsToRemove);
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    XClusterConfig xClusterConfig = getXClusterConfigFromTaskParams();
    Map<String, String> tableIdsToAddBootstrapIdsMap = new HashMap<>();
    Set<String> tableIdsToAdd =
        taskParams().tableIdsToAdd == null ? new HashSet<>() : taskParams().tableIdsToAdd;
    Set<String> tableIdsToRemove =
        taskParams().tableIdsToRemove == null ? new HashSet<>() : taskParams().tableIdsToRemove;

    // Ensure the tableIdsToAdd and tableIdsToRemove sets are disjoint.
    if (tableIdsToAdd.stream().anyMatch(tableIdsToRemove::contains)) {
      throw new IllegalArgumentException(
          "taskParams().tableIdsToAdd and "
              + "taskParams().tableIdsToRemove sets must be disjoint");
    }

    // Ensure each tableId exits in the xCluster config and replication is not set up for it. Also,
    // get the bootstrapIds if there is any.
    for (String tableId : tableIdsToAdd) {
      Optional<XClusterTableConfig> tableConfig = xClusterConfig.maybeGetTableById(tableId);
      if (!tableConfig.isPresent()) {
        String errMsg =
            String.format(
                "Table with id (%s) does not belong to the task params xCluster config (%s)",
                tableId, xClusterConfig.uuid);
        throw new IllegalArgumentException(errMsg);
      }
      if (tableConfig.get().replicationSetupDone) {
        String errMsg =
            String.format(
                "Replication is already set up for table with id (%s) and cannot be set up again",
                tableId);
        throw new IllegalArgumentException(errMsg);
      }
      tableIdsToAddBootstrapIdsMap.put(tableId, tableConfig.get().streamId);
    }
    // Either all tables should need bootstrap, or none should.
    if (tableIdsToAddBootstrapIdsMap.values().stream().anyMatch(Objects::isNull)
        && tableIdsToAddBootstrapIdsMap.values().stream().anyMatch(Objects::nonNull)) {
      throw new IllegalArgumentException(
          String.format(
              "Failed to modify XClusterConfig(%s) to add tables because some tables went "
                  + "through bootstrap and some did not, You must create modify tables subtasks "
                  + "separately for them",
              xClusterConfig.uuid));
    }

    // Ensure each tableId exits in the xCluster config.
    for (String tableId : tableIdsToRemove) {
      Optional<XClusterTableConfig> tableConfig = xClusterConfig.maybeGetTableById(tableId);
      if (!tableConfig.isPresent()) {
        String errMsg =
            String.format(
                "Table with id (%s) is not part of xCluster config (%s) and cannot be removed",
                tableId, xClusterConfig.uuid);
        throw new IllegalArgumentException(errMsg);
      }
    }
    log.info("Modifying tables in XClusterConfig({})", xClusterConfig.uuid);

    Universe targetUniverse = Universe.getOrBadRequest(xClusterConfig.targetUniverseUUID);
    String targetUniverseMasterAddresses = targetUniverse.getMasterAddresses();
    String targetUniverseCertificate = targetUniverse.getCertificateNodetoNode();
    YBClient client = ybService.getClient(targetUniverseMasterAddresses, targetUniverseCertificate);
    try {
      if (tableIdsToAddBootstrapIdsMap.size() > 0) {
        log.info(
            "Adding tables to XClusterConfig({}): tableIdsToAddBootstrapIdsMap {}",
            xClusterConfig.uuid,
            tableIdsToAddBootstrapIdsMap);
        AlterUniverseReplicationResponse resp =
            client.alterUniverseReplicationAddTables(
                xClusterConfig.getReplicationGroupName(), tableIdsToAddBootstrapIdsMap);
        if (resp.hasError()) {
          String errMsg =
              String.format(
                  "Failed to add tables to XClusterConfig(%s): %s",
                  xClusterConfig.uuid, resp.errorMessage());
          throw new RuntimeException(errMsg);
        }
        waitForXClusterOperation(client::isAlterUniverseReplicationDone);

        // Persist that replicationSetupDone is true for the tables in taskParams. We have checked
        // that taskParams().tableIdsToAdd exist in the xCluster config, so it will not throw an
        // exception.
        xClusterConfig.setReplicationSetupDone(taskParams().tableIdsToAdd);

        // Get the stream ids from the target universe and put it in the Platform DB for the
        // added tables to the xCluster config.
        CatalogEntityInfo.SysClusterConfigEntryPB clusterConfig =
            getClusterConfig(client, targetUniverse.universeUUID);
        updateStreamIdsFromTargetUniverseClusterConfig(
            clusterConfig, xClusterConfig, taskParams().tableIdsToAdd);

        if (HighAvailabilityConfig.get().isPresent()) {
          // Note: We increment version twice for adding tables: once for setting up the .ALTER
          // replication group, and once for merging the .ALTER replication group
          getUniverse(true).incrementVersion();
          getUniverse(true).incrementVersion();
        }
      }

      if (tableIdsToRemove.size() > 0) {
        log.info(
            "Removing tables from XClusterConfig({}): {}", xClusterConfig.uuid, tableIdsToRemove);
        // Sync the state for the tables to be removed.
        CatalogEntityInfo.SysClusterConfigEntryPB clusterConfig =
            getClusterConfig(client, targetUniverse.universeUUID);
        boolean replicationGroupExists =
            syncReplicationSetUpStateForTables(clusterConfig, xClusterConfig, tableIdsToRemove);
        if (!replicationGroupExists) {
          throw new RuntimeException(
              String.format(
                  "No replication group found with name (%s) in universe (%s) cluster config",
                  xClusterConfig.getReplicationGroupName(), xClusterConfig.targetUniverseUUID));
        }
        Set<String> tableIdsToRemoveWithReplication =
            xClusterConfig
                .getTableIdsWithReplicationSetup()
                .stream()
                .filter(tableIdsToRemove::contains)
                .collect(Collectors.toSet());
        log.debug(
            "Table IDs to remove with replication set up: {}", tableIdsToRemoveWithReplication);

        // Removing all tables from a replication config is not allowed.
        if (tableIdsToRemoveWithReplication.size()
                + xClusterConfig.getTableIdsWithReplicationSetup(false /* done */).size()
            == xClusterConfig.getTables().size()) {
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
                    xClusterConfig.uuid, resp.errorMessage()));
          }

          if (HighAvailabilityConfig.get().isPresent()) {
            getUniverse(true).incrementVersion();
          }
        }
        xClusterConfig.removeTables(tableIdsToRemove);
      }
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    } finally {
      ybService.closeClient(client, targetUniverseMasterAddresses);
    }

    log.info("Completed {}", getName());
  }
}
