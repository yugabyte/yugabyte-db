// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.commissioner.tasks.subtasks.xcluster;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.common.XClusterUtil;
import com.yugabyte.yw.forms.XClusterConfigTaskParams;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterConfig.ConfigType;
import com.yugabyte.yw.models.XClusterTableConfig;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.Set;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.CommonNet;
import org.yb.cdc.CdcConsumer;
import org.yb.cdc.CdcConsumer.XClusterRole;
import org.yb.client.DeleteUniverseReplicationResponse;
import org.yb.client.GetMasterClusterConfigResponse;
import org.yb.client.SetupUniverseReplicationResponse;
import org.yb.client.YBClient;
import org.yb.master.CatalogEntityInfo;
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
  protected XClusterConfigSetup(
      BaseTaskDependencies baseTaskDependencies, XClusterUniverseService xClusterUniverseService) {
    super(baseTaskDependencies, xClusterUniverseService);
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
        "%s (targetUniverse=%s, xClusterUuid=%s)",
        super.getName(),
        taskParams().getUniverseUUID(),
        taskParams().getXClusterConfig().getUuid());
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

    Universe sourceUniverse = Universe.getOrBadRequest(xClusterConfig.getSourceUniverseUUID());
    Universe targetUniverse = Universe.getOrBadRequest(xClusterConfig.getTargetUniverseUUID());
    String targetUniverseMasterAddresses = targetUniverse.getMasterAddresses();
    String targetUniverseCertificate = targetUniverse.getCertificateNodetoNode();
    try (YBClient client =
        ybService.getClient(targetUniverseMasterAddresses, targetUniverseCertificate)) {
      try {
        CatalogEntityInfo.SysClusterConfigEntryPB clusterConfig =
            getClusterConfig(client, targetUniverse.getUniverseUUID());
        CdcConsumer.ProducerEntryPB existingReplicationGroup =
            clusterConfig
                .getConsumerRegistry()
                .getProducerMapMap()
                .get(xClusterConfig.getReplicationGroupName());
        if (Objects.nonNull(existingReplicationGroup)) {
          syncXClusterConfigWithReplicationGroup(
              clusterConfig, xClusterConfig, taskParams().tableIds);
          // We do not need to set the role because in the versions of YBDB that we are going to
          // support retry-ability, the role is set automatically.
          log.info("Skipping {}: replication group already exists", getName());
          return;
        }
      } catch (Exception e) {
        log.warn("Failed to get cluster config from target universe: {}", e.getMessage());
      }

      // Find bootstrap ids, and check replication is not already set up for that table.
      Map<String, String> tableIdsBootstrapIdsMap = new HashMap<>();
      for (String tableId : taskParams().tableIds) {
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
                  "Replication is already set up for table with id (%s) in xCluster config (%s)",
                  tableId, xClusterConfig.getUuid());
          throw new IllegalArgumentException(errMsg);
        }
        tableIdsBootstrapIdsMap.put(tableId, tableConfig.get().getStreamId());
      }
      // Either all tables should need bootstrap, or none should.
      if (tableIdsBootstrapIdsMap.values().stream().anyMatch(Objects::isNull)
          && tableIdsBootstrapIdsMap.values().stream().anyMatch(Objects::nonNull)) {
        throw new IllegalArgumentException(
            String.format(
                "Failed to create XClusterConfig(%s) because some tables went through bootstrap and"
                    + " some did not, You must create XClusterConfigSetup subtask separately for"
                    + " them",
                xClusterConfig.getUuid()));
      }

      log.info(
          "Setting up replication for XClusterConfig({}): tableIdsBootstrapIdsMap {}",
          xClusterConfig.getUuid(),
          tableIdsBootstrapIdsMap);
      // For dual NIC, the universes will be able to communicate over the secondary
      // addresses.
      Set<CommonNet.HostPortPB> sourceMasterAddresses =
          new HashSet<>(
              NetUtil.parseStringsAsPB(
                  sourceUniverse.getMasterAddresses(
                      false /* mastersQueryable */, true /* getSecondary */)));

      // Sometimes, the replication group is created in an intermittent state but it does not show
      // up in the universe's cluster config. In those cases, the SetupUniverseReplication RPC
      // fails. The workaround is to delete the replication group and then create it again.
      SetupUniverseReplicationResponse resp;
      try {
        resp =
            client.setupUniverseReplication(
                xClusterConfig.getReplicationGroupName(),
                tableIdsBootstrapIdsMap,
                sourceMasterAddresses,
                supportsTxnXCluster(targetUniverse)
                    ? xClusterConfig.getType().equals(ConfigType.Txn)
                    : null);
        if (resp.hasError()) {
          throw new RuntimeException(
              String.format(
                  "Failed to set up replication for XClusterConfig(%s): %s",
                  xClusterConfig.getUuid(), resp.errorMessage()));
        }
      } catch (Exception e) {
        log.warn(
            "Failed to set up replication for XClusterConfig({}): {}",
            xClusterConfig.getUuid(),
            e.getMessage());
        if (!e.getMessage().contains("already present")) {
          throw e;
        }
        // Delete the replication group because it already exists and try again.
        DeleteUniverseReplicationResponse deleteReplicationResp =
            client.deleteUniverseReplication(
                xClusterConfig.getReplicationGroupName(), true /* ignoreErrors */);
        // Log the warnings in response.
        String deleteReplicationRespWarnings = deleteReplicationResp.getWarningsString();
        if (deleteReplicationRespWarnings != null) {
          log.warn(
              "During deleteUniverseReplication, the following warnings occurred: {}",
              deleteReplicationRespWarnings);
        }
        if (deleteReplicationResp.hasError()) {
          throw new RuntimeException(
              String.format(
                  "Failed to delete replication for XClusterConfig(%s): %s",
                  xClusterConfig.getUuid(), deleteReplicationResp.errorMessage()));
        }
        resp =
            client.setupUniverseReplication(
                xClusterConfig.getReplicationGroupName(),
                tableIdsBootstrapIdsMap,
                sourceMasterAddresses,
                supportsTxnXCluster(targetUniverse)
                    ? xClusterConfig.getType().equals(ConfigType.Txn)
                    : null);
        if (resp.hasError()) {
          throw new RuntimeException(
              String.format(
                  "Failed to set up replication for XClusterConfig(%s): %s",
                  xClusterConfig.getUuid(), resp.errorMessage()));
        }
      }
      waitForXClusterOperation(xClusterConfig, client::isSetupUniverseReplicationDone);

      // Get the stream ids from the target universe and put it in the Platform DB.
      GetMasterClusterConfigResponse clusterConfigResp = client.getMasterClusterConfig();
      if (clusterConfigResp.hasError()) {
        String errMsg =
            String.format(
                "Failed to getMasterClusterConfig from target universe (%s) for xCluster config "
                    + "(%s): %s",
                targetUniverse.getUniverseUUID(),
                xClusterConfig.getUuid(),
                clusterConfigResp.errorMessage());
        throw new RuntimeException(errMsg);
      }
      syncXClusterConfigWithReplicationGroup(
          clusterConfigResp.getConfig(), xClusterConfig, taskParams().tableIds);

      // For txn xCluster set the target universe role to standby.
      // But from "2024.1.0.0-b71/2.23.0.0-b157" onwards, we support multiple txn replication
      // so we don't need to set the role to STANDBY as we will have this role per DBs which is
      // handled by DB itself.
      if (xClusterConfig.getType().equals(ConfigType.Txn)
          && xClusterConfig.isTargetActive()
          && !XClusterUtil.supportMultipleTxnReplication(targetUniverse)) {
        log.info("Setting the role of universe {} to STANDBY", targetUniverse.getUniverseUUID());
        client.changeXClusterRole(XClusterRole.STANDBY);
        xClusterConfig.setTargetActive(false);
        xClusterConfig.update();
      }

      if (HighAvailabilityConfig.get().isPresent()) {
        getUniverse().incrementVersion();
      }
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    }

    log.info("Completed {}", getName());
  }
}
