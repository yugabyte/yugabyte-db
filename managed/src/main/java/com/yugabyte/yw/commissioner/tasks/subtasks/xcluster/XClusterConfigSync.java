// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.commissioner.tasks.subtasks.xcluster;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterConfig.ConfigType;
import com.yugabyte.yw.models.XClusterConfig.XClusterConfigStatusType;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.cdc.CdcConsumer;
import org.yb.cdc.CdcConsumer.ProducerEntryPB;
import org.yb.client.YBClient;
import org.yb.master.CatalogEntityInfo;
import org.yb.master.MasterDdlOuterClass;

@Slf4j
public class XClusterConfigSync extends XClusterConfigTaskBase {

  @Inject
  protected XClusterConfigSync(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    Universe targetUniverse = Universe.getOrBadRequest(taskParams().universeUUID);
    String targetUniverseMasterAddresses = targetUniverse.getMasterAddresses();
    String targetUniverseCertificate = targetUniverse.getCertificateNodetoNode();
    try (YBClient client =
        ybService.getClient(targetUniverseMasterAddresses, targetUniverseCertificate)) {
      CatalogEntityInfo.SysClusterConfigEntryPB clusterConfig =
          getClusterConfig(client, targetUniverse.universeUUID);
      syncXClusterConfigs(clusterConfig, targetUniverse.universeUUID);
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    }

    log.info("Completed {}", getName());
  }

  private void syncXClusterConfigs(
      CatalogEntityInfo.SysClusterConfigEntryPB config, UUID targetUniverseUUID) {
    Map<String, ProducerEntryPB> replicationGroups =
        new HashMap<>(config.getConsumerRegistry().getProducerMapMap());

    // Try to find the txn replication group.
    Optional<ProducerEntryPB> txnReplicationGroupOptional =
        Optional.ofNullable(
            replicationGroups.remove(TRANSACTION_STATUS_TABLE_REPLICATION_GROUP_NAME));

    // Import all the xCluster configs on the target universe cluster config.
    Set<Pair<UUID, String>> foundXClusterConfigs = new HashSet<>();
    replicationGroups.forEach(
        (replicationGroupName, value) -> {
          // Parse and get information for this replication group.
          Optional<Pair<UUID, String>> sourceUuidAndConfigName =
              maybeParseReplicationGroupName(replicationGroupName);
          if (!sourceUuidAndConfigName.isPresent()) {
            log.warn(
                "Skipping {} because it does not conform to the Platform replication group naming",
                replicationGroupName);
            return;
          }
          UUID sourceUniverseUUID = sourceUuidAndConfigName.get().getFirst();
          String xClusterConfigName = sourceUuidAndConfigName.get().getSecond();
          foundXClusterConfigs.add(sourceUuidAndConfigName.get());
          // Get table ids for this replication group.
          Map<String, CdcConsumer.StreamEntryPB> tableMap = value.getStreamMapMap();
          Set<String> xClusterConfigTables =
              tableMap
                  .values()
                  .stream()
                  .map(CdcConsumer.StreamEntryPB::getProducerTableId)
                  .collect(Collectors.toSet());
          log.info(
              "Found XClusterConfig({}) between source({}) and target({}): disabled({}), "
                  + "tables({})",
              xClusterConfigName,
              sourceUniverseUUID,
              targetUniverseUUID,
              value.getDisableStream(),
              xClusterConfigTables);

          // Create or update a row in the Platform database for this replication group.
          XClusterConfig xClusterConfig =
              XClusterConfig.getByNameSourceTarget(
                  xClusterConfigName, sourceUniverseUUID, targetUniverseUUID);
          if (xClusterConfig == null) {
            xClusterConfig =
                XClusterConfig.create(
                    xClusterConfigName,
                    sourceUniverseUUID,
                    targetUniverseUUID,
                    txnReplicationGroupOptional.isPresent() ? ConfigType.Txn : ConfigType.Basic);
            log.info("Creating new XClusterConfig({})", xClusterConfig.uuid);
          } else {
            log.info("Updating existing XClusterConfig({})", xClusterConfig);
          }
          xClusterConfig.setStatus(XClusterConfigStatusType.Running);
          xClusterConfig.setPaused(value.getDisableStream());
          xClusterConfig.addTablesIfNotExist(xClusterConfigTables);

          // Set txn table id for txn configs. We assume the source universe is always in active
          // role, and we do not sync it.
          if (txnReplicationGroupOptional.isPresent()) {
            List<MasterDdlOuterClass.ListTablesResponsePB.TableInfo> sourceTableInfoList =
                getTableInfoList(
                    ybService,
                    Universe.getOrBadRequest(sourceUniverseUUID),
                    false /* excludeSystemTables */);
            Optional<MasterDdlOuterClass.ListTablesResponsePB.TableInfo>
                txnTableInfoSourceOptional = getTxnTableInfoIfExists(sourceTableInfoList);
            if (!txnTableInfoSourceOptional.isPresent()) {
              throw new IllegalStateException(
                  String.format(
                      "The detected xCluster config type for %s is %s, but %s.%s on the "
                          + "source universe is not found",
                      xClusterConfig.uuid,
                      ConfigType.Txn,
                      TRANSACTION_STATUS_TABLE_NAMESPACE,
                      TRANSACTION_STATUS_TABLE_NAME));
            }
            xClusterConfig.setTxnTableId(
                Collections.singletonList(txnTableInfoSourceOptional.get()));
          }

          syncXClusterConfigWithReplicationGroup(
              config, xClusterConfig, xClusterConfigTables, false /* skipSyncTxnTable */);
        });

    List<XClusterConfig> currentXClusterConfigsForTarget =
        XClusterConfig.getByTargetUniverseUUID(targetUniverseUUID);
    for (XClusterConfig xClusterConfig : currentXClusterConfigsForTarget) {
      if (!foundXClusterConfigs.contains(
          new Pair<>(xClusterConfig.sourceUniverseUUID, xClusterConfig.name))) {
        xClusterConfig.delete();
        log.info("Deleted unknown XClusterConfig({})", xClusterConfig.uuid);
      }
    }
  }
}
