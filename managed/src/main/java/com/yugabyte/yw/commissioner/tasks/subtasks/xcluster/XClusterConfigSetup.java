// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.commissioner.tasks.subtasks.xcluster;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.forms.XClusterConfigTaskParams;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterTableConfig;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;
import java.util.function.Function;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.GetMasterClusterConfigResponse;
import org.yb.client.SetupUniverseReplicationResponse;
import org.yb.client.YBClient;
import org.yb.util.NetUtil;

/**
 * This subtask will set up xCluster replication for the set of tableIds passed in.
 *
 * <p>Note: It does not need to check if setting up an xCluster replication is impossible due to
 * garbage-collected WALs because the coreDB checks it and returns an error if that is the case.
 */
@Slf4j
public class XClusterConfigSetup extends XClusterConfigTaskBase {

  @Inject
  protected XClusterConfigSetup(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends XClusterConfigTaskParams {
    // The target universe UUID must be stored in universeUUID field.
    // The parent xCluster config must be stored in xClusterConfig field.
    // Table ids to set up replication for.
    public Set<String> tableIds;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public String getName() {
    return String.format(
        "%s (targetUniverse=%s, xClusterUuid=%s, tableIds=%s)",
        super.getName(),
        taskParams().universeUUID,
        taskParams().xClusterConfig.uuid,
        taskParams().tableIds);
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    // Each replication setup task must belong to a parent xCluster config.
    XClusterConfig xClusterConfig = getXClusterConfigFromTaskParams();
    // TableIds in the task parameters must not be null or empty.
    if (taskParams().tableIds == null || taskParams().tableIds.isEmpty()) {
      throw new RuntimeException(
          String.format(
              "`tableIds` in the task parameters must not be null or empty: it was %s",
              taskParams().tableIds));
    }

    // Find bootstrap ids, and check replication is not already set up for that table.
    Map<String, String> tableIdsBootstrapIdsMap = new HashMap<>();
    for (String tableId : taskParams().tableIds) {
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
                "Replication is already set up for table with id (%s) in xCluster config (%s)",
                tableId, xClusterConfig.uuid);
        throw new IllegalArgumentException(errMsg);
      }
      tableIdsBootstrapIdsMap.put(tableId, tableConfig.get().streamId);
    }
    // Either all tables should need bootstrap, or none should.
    if (tableIdsBootstrapIdsMap.values().stream().anyMatch(Objects::isNull)
        && tableIdsBootstrapIdsMap.values().stream().anyMatch(Objects::nonNull)) {
      throw new IllegalArgumentException(
          String.format(
              "Failed to create XClusterConfig(%s) because some tables went through bootstrap and "
                  + "some did not, You must create XClusterConfigSetup subtask separately for them",
              xClusterConfig.uuid));
    }

    Universe sourceUniverse = Universe.getOrBadRequest(xClusterConfig.sourceUniverseUUID);
    Universe targetUniverse = Universe.getOrBadRequest(xClusterConfig.targetUniverseUUID);
    String targetUniverseMasterAddresses = targetUniverse.getMasterAddresses();
    String targetUniverseCertificate = targetUniverse.getCertificateNodetoNode();
    YBClient client = ybService.getClient(targetUniverseMasterAddresses, targetUniverseCertificate);

    try {
      log.info(
          "Setting up replication for XClusterConfig({}): tableIdsBootstrapIdsMap {}",
          xClusterConfig.uuid,
          tableIdsBootstrapIdsMap);
      SetupUniverseReplicationResponse resp =
          client.setupUniverseReplication(
              xClusterConfig.getReplicationGroupName(),
              tableIdsBootstrapIdsMap,
              // For dual NIC, the universes will be able to communicate over the secondary
              // addresses.
              new HashSet<>(
                  NetUtil.parseStringsAsPB(
                      sourceUniverse.getMasterAddresses(
                          false /* mastersQueryable */, true /* getSecondary */))));
      if (resp.hasError()) {
        throw new RuntimeException(
            String.format(
                "Failed to set up replication for XClusterConfig(%s): %s",
                xClusterConfig.uuid, resp.errorMessage()));
      }
      waitForXClusterOperation(client::isSetupUniverseReplicationDone);

      // Persist that replicationSetupDone is true for the tables in taskParams. We have checked
      // that taskParams().tableIds exist in the xCluster config, so it will not throw an exception.
      xClusterConfig.setReplicationSetupDone(taskParams().tableIds);

      // Get the stream ids from the target universe and put it in the Platform DB.
      GetMasterClusterConfigResponse clusterConfigResp = client.getMasterClusterConfig();
      if (clusterConfigResp.hasError()) {
        String errMsg =
            String.format(
                "Failed to getMasterClusterConfig from target universe (%s) for xCluster config "
                    + "(%s): %s",
                targetUniverse.universeUUID, xClusterConfig.uuid, clusterConfigResp.errorMessage());
        throw new RuntimeException(errMsg);
      }
      updateStreamIdsFromTargetUniverseClusterConfig(
          clusterConfigResp.getConfig(), xClusterConfig, taskParams().tableIds);

      if (HighAvailabilityConfig.get().isPresent()) {
        getUniverse(true).incrementVersion();
      }
    } catch (Exception e) {
      log.error("{} hit erro  r : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    } finally {
      ybService.closeClient(client, targetUniverseMasterAddresses);
    }

    log.info("Completed {}", getName());
  }
}
