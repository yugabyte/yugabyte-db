// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.commissioner.tasks.subtasks.xcluster;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.forms.XClusterConfigTaskParams;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterConfig.ConfigType;
import com.yugabyte.yw.models.XClusterTableConfig;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.DeleteUniverseReplicationResponse;
import org.yb.client.MasterErrorException;
import org.yb.client.YBClient;
import org.yb.master.CatalogEntityInfo;

@Slf4j
public class DeleteReplication extends XClusterConfigTaskBase {

  @Inject
  protected DeleteReplication(
      BaseTaskDependencies baseTaskDependencies, XClusterUniverseService xClusterUniverseService) {
    super(baseTaskDependencies, xClusterUniverseService);
  }

  public static class Params extends XClusterConfigTaskParams {
    // The target universe UUID must be stored in universeUUID field.
    // The parent xCluster config must be stored in xClusterConfig field.
    // Whether the client RPC call ignore errors during replication deletion.
    public boolean ignoreErrors;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public String getName() {
    return String.format(
        "%s (xClusterConfig=%s, ignoreErrors=%s)",
        super.getName(), taskParams().getXClusterConfig(), taskParams().ignoreErrors);
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    XClusterConfig xClusterConfig = getXClusterConfigFromTaskParams();

    if (xClusterConfig.getTargetUniverseUUID() == null) {
      xClusterConfig.updateReplicationSetupDone(
          xClusterConfig.getTableIds(), false /* replicationSetupDone */);
      log.info("Skipped {}: the target universe is destroyed", getName());
      return;
    }

    // Ignore errors when it is requested by the user or source universe is deleted.
    boolean ignoreErrors =
        taskParams().ignoreErrors || xClusterConfig.getSourceUniverseUUID() == null;

    Universe targetUniverse = Universe.getOrBadRequest(xClusterConfig.getTargetUniverseUUID());
    String targetUniverseMasterAddresses = targetUniverse.getMasterAddresses();
    String targetUniverseCertificate = targetUniverse.getCertificateNodetoNode();
    try (YBClient client =
        ybService.getClient(targetUniverseMasterAddresses, targetUniverseCertificate)) {

      if (xClusterConfig.getType() != ConfigType.Db) {
        // Sync the state for the tables.
        CatalogEntityInfo.SysClusterConfigEntryPB clusterConfig =
            getClusterConfig(client, targetUniverse.getUniverseUUID());
        boolean replicationGroupExists =
            syncReplicationSetUpStateForTables(
                clusterConfig, xClusterConfig, xClusterConfig.getTableIds());
        if (!replicationGroupExists) {
          log.warn("No replication group found for {}", xClusterConfig);
        }
      }

      // Catch the `Universe replication NOT_FOUND` exception, and because it already does not
      // exist, the exception will be ignored. We run the RPC even when the target universe
      // cluster config did not have the replication group to preserve the old behavior, and it
      // is useful in case YBDB returns some warnings to be printed in the YBA logs.
      try {
        DeleteUniverseReplicationResponse resp =
            client.deleteUniverseReplication(
                xClusterConfig.getReplicationGroupName(), ignoreErrors);
        // Log the warnings in response.
        String respWarnings = resp.getWarningsString();
        if (respWarnings != null) {
          log.warn(
              "During deleteUniverseReplication, the following warnings occurred: {}",
              respWarnings);
        }
        if (resp.hasError()) {
          throw new RuntimeException(
              String.format(
                  "Failed to delete replication for XClusterConfig(%s): %s",
                  xClusterConfig.getUuid(), resp.errorMessage()));
        }
      } catch (MasterErrorException e) {
        // If it is not `Universe replication NOT_FOUND` exception, rethrow the exception.
        if (!e.getMessage().contains("NOT_FOUND[code 1]: Universe replication")) {
          throw new RuntimeException(e);
        }
        log.warn(
            "XCluster config {} does not exist on the target universe, NOT_FOUND exception "
                + "occurred in deleteUniverseReplication RPC call is ignored: {}",
            xClusterConfig.getUuid(),
            e.getMessage());
      }
      if (xClusterConfig.getType() != ConfigType.Db) {
        // After the RPC call, the corresponding stream ids on the source universe will be deleted
        // as well.
        xClusterConfig
            .getTablesById(
                xClusterConfig.getTableIdsWithReplicationSetup(
                    xClusterConfig.getTableIds(), true /* done */))
            .forEach(XClusterTableConfig::reset);
        xClusterConfig.update();
      }

      if (HighAvailabilityConfig.get().isPresent()) {
        getUniverse().incrementVersion();
      }
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      if (!taskParams().ignoreErrors) {
        throw new RuntimeException(e);
      }
    }

    log.info("Completed {}", getName());
  }
}
