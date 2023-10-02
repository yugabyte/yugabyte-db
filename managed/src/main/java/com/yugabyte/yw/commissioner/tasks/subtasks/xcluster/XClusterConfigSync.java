// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.commissioner.tasks.subtasks.xcluster;

import com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.forms.XClusterConfigSyncFormData;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterConfig.ConfigType;
import com.yugabyte.yw.models.XClusterConfig.XClusterConfigStatusType;
import java.util.Arrays;
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
import org.yb.CommonNet.HostPortPB;
import org.yb.cdc.CdcConsumer;
import org.yb.cdc.CdcConsumer.ProducerEntryPB;
import org.yb.client.YBClient;
import org.yb.master.CatalogEntityInfo;

@Slf4j
public class XClusterConfigSync extends XClusterConfigTaskBase {

  @Inject
  protected XClusterConfigSync(
      BaseTaskDependencies baseTaskDependencies, XClusterUniverseService xClusterUniverseService) {
    super(baseTaskDependencies, xClusterUniverseService);
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    Universe targetUniverse = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    String targetUniverseMasterAddresses = targetUniverse.getMasterAddresses();
    String targetUniverseCertificate = targetUniverse.getCertificateNodetoNode();
    try (YBClient client =
        ybService.getClient(targetUniverseMasterAddresses, targetUniverseCertificate)) {
      CatalogEntityInfo.SysClusterConfigEntryPB clusterConfig =
          getClusterConfig(client, targetUniverse.getUniverseUUID());
      XClusterConfigSyncFormData syncFormData = taskParams().getSyncFormData();
      if (syncFormData != null) {
        syncXClusterConfig(clusterConfig, targetUniverse, syncFormData.replicationGroupName);
      } else {
        syncXClusterConfigs(clusterConfig, targetUniverse.getUniverseUUID());
      }
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    }

    log.info("Completed {}", getName());
  }

  private void syncXClusterConfig(
      CatalogEntityInfo.SysClusterConfigEntryPB config,
      Universe targetUniverse,
      String replicationGroupName) {
    Customer customer = Customer.get(targetUniverse.getCustomerId());
    Map<String, ProducerEntryPB> replicationGroups =
        new HashMap<>(config.getConsumerRegistry().getProducerMapMap());

    ProducerEntryPB replicationGroupEntry = replicationGroups.get(replicationGroupName);
    if (replicationGroupEntry == null) {
      throw new RuntimeException(
          String.format(
              "No replication group found for replication group name: %s", replicationGroupName));
    }

    Set<Universe> candidateUniverses = customer.getUniverses();
    Map<String, Universe> hostUniverseMap = new HashMap<>();
    for (Universe candidateUniverse : candidateUniverses) {
      Arrays.stream(candidateUniverse.getMasterAddresses().split(","))
          .map(HostAndPort::fromString)
          .map(hp -> hp.getHost())
          .forEach(host -> hostUniverseMap.put(host, candidateUniverse));
    }

    Universe sourceUniverse = null;
    for (HostPortPB hostPortPB : replicationGroupEntry.getMasterAddrsList()) {
      sourceUniverse = hostUniverseMap.get(hostPortPB.getHost());
      if (sourceUniverse != null) {
        break;
      }
    }

    // No source universe found for given replication group name.
    if (sourceUniverse == null) {
      throw new RuntimeException(
          String.format(
              "Could not find corresponding source universe for replication group name: %s",
              replicationGroupName));
    }

    XClusterConfig xClusterConfig =
        XClusterConfig.getByReplicationGroupNameTarget(
            replicationGroupName, targetUniverse.getUniverseUUID());
    if (xClusterConfig == null) {
      xClusterConfig =
          XClusterConfig.create(
              replicationGroupName,
              sourceUniverse.getUniverseUUID(),
              targetUniverse.getUniverseUUID(),
              config.getConsumerRegistry().getTransactional() ? ConfigType.Txn : ConfigType.Basic,
              true /* imported */);
      log.info("Creating new XClusterConfig({})", xClusterConfig.getUuid());
    } else {
      // If xClusterConfig already exists, we will not change its 'imported' state. As the xcluster
      // config may or not conform to the YBA naming style.
      log.info("Updating existing XClusterConfig({})", xClusterConfig);
    }
    updateAndSyncXClusterConfig(xClusterConfig, replicationGroupEntry, config);
  }

  private void syncXClusterConfigs(
      CatalogEntityInfo.SysClusterConfigEntryPB config, UUID targetUniverseUUID) {
    Map<String, ProducerEntryPB> replicationGroups =
        new HashMap<>(config.getConsumerRegistry().getProducerMapMap());

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
              tableMap.values().stream()
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
                    config.getConsumerRegistry().getTransactional()
                        ? ConfigType.Txn
                        : ConfigType.Basic);
            log.info("Creating new XClusterConfig({})", xClusterConfig.getUuid());
          } else {
            log.info("Updating existing XClusterConfig({})", xClusterConfig);
          }
          updateAndSyncXClusterConfig(xClusterConfig, value, config);
        });

    List<XClusterConfig> currentXClusterConfigsForTarget =
        XClusterConfig.getByTargetUniverseUUID(targetUniverseUUID);
    for (XClusterConfig xClusterConfig : currentXClusterConfigsForTarget) {
      if (!xClusterConfig.isImported()
          && !foundXClusterConfigs.contains(
              new Pair<>(xClusterConfig.getSourceUniverseUUID(), xClusterConfig.getName()))) {
        xClusterConfig.delete();
        log.info("Deleted unknown XClusterConfig({})", xClusterConfig.getUuid());
      }
    }
  }

  private void updateAndSyncXClusterConfig(
      XClusterConfig xClusterConfig,
      ProducerEntryPB replicationGroupEntry,
      CatalogEntityInfo.SysClusterConfigEntryPB config) {
    Map<String, CdcConsumer.StreamEntryPB> tableMap = replicationGroupEntry.getStreamMapMap();
    Set<String> xClusterConfigTables =
        tableMap.values().stream()
            .map(CdcConsumer.StreamEntryPB::getProducerTableId)
            .collect(Collectors.toSet());

    xClusterConfig.setStatus(XClusterConfigStatusType.Running);
    xClusterConfig.setPaused(replicationGroupEntry.getDisableStream());
    xClusterConfig.syncTables(xClusterConfigTables);

    syncXClusterConfigWithReplicationGroup(config, xClusterConfig, xClusterConfigTables);
  }
}
