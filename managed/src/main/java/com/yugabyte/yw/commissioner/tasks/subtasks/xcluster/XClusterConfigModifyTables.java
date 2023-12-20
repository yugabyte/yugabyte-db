// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.commissioner.tasks.subtasks.xcluster;

import com.google.common.collect.Sets;
import com.google.common.collect.Sets.SetView;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import java.util.Set;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.AlterUniverseReplicationResponse;
import org.yb.client.YBClient;

@Slf4j
public class XClusterConfigModifyTables extends XClusterConfigTaskBase {

  @Inject
  protected XClusterConfigModifyTables(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  public void initialize(ITaskParams params) {
    super.initialize(params);
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    XClusterConfig xClusterConfig = refreshXClusterConfig();
    Universe targetUniverse = Universe.getOrBadRequest(xClusterConfig.targetUniverseUUID);

    String targetUniverseMasterAddresses = targetUniverse.getMasterAddresses();
    String targetUniverseCertificate = targetUniverse.getCertificateNodetoNode();
    YBClient client = ybService.getClient(targetUniverseMasterAddresses, targetUniverseCertificate);

    Set<String> currentTables = xClusterConfig.getTables();
    Set<String> desiredTables = taskParams().editFormData.tables;
    SetView<String> tablesToAdd = Sets.difference(desiredTables, currentTables);
    SetView<String> tablesToRemove = Sets.difference(currentTables, desiredTables);

    try {
      log.info("Modifying tables in XClusterConfig({}): {}", xClusterConfig.uuid, currentTables);

      if (tablesToAdd.size() > 0) {
        log.info("Adding tables to XClusterConfig({}): {}", xClusterConfig.uuid, tablesToAdd);
        AlterUniverseReplicationResponse resp =
            client.alterUniverseReplicationAddTables(
                xClusterConfig.getReplicationGroupName(), tablesToAdd);
        if (resp.hasError()) {
          String errMsg =
              String.format(
                  "Failed to add tables to XClusterConfig(%s): %s",
                  xClusterConfig.uuid, resp.errorMessage());
          throw new RuntimeException(errMsg);
        }

        waitForXClusterOperation(client::isAlterUniverseReplicationDone);

        if (HighAvailabilityConfig.get().isPresent()) {
          // Note: We increment version twice for adding tables: once for setting up the .ALTER
          // replication group, and once for merging the .ALTER replication group
          getUniverse().incrementVersion();
          getUniverse().incrementVersion();
        }
      }

      if (tablesToRemove.size() > 0) {
        log.info(
            "Removing tables from XClusterConfig({}): {}", xClusterConfig.uuid, tablesToRemove);
        AlterUniverseReplicationResponse resp =
            client.alterUniverseReplicationRemoveTables(
                xClusterConfig.getReplicationGroupName(), tablesToRemove);
        if (resp.hasError()) {
          String errMsg =
              String.format(
                  "Failed to remove tables from XClusterConfig(%s): %s",
                  xClusterConfig.uuid, resp.errorMessage());
          throw new RuntimeException(errMsg);
        }

        if (HighAvailabilityConfig.get().isPresent()) {
          getUniverse().incrementVersion();
        }
      }

      xClusterConfig.setTables(desiredTables);
      xClusterConfig.update();

    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    } finally {
      ybService.closeClient(client, targetUniverseMasterAddresses);
    }

    log.info("Completed {}", getName());
  }
}
