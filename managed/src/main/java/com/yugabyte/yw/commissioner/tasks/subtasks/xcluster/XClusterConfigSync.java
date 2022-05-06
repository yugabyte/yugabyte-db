package com.yugabyte.yw.commissioner.tasks.subtasks.xcluster;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.forms.XClusterConfigCreateFormData;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterConfig.XClusterConfigStatusType;
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
import org.yb.client.GetMasterClusterConfigResponse;
import org.yb.client.YBClient;
import org.yb.master.CatalogEntityInfo;

@Slf4j
public class XClusterConfigSync extends XClusterConfigTaskBase {

  @Inject
  protected XClusterConfigSync(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  public void initialize(ITaskParams params) {
    super.initialize(params);
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    Universe targetUniverse = Universe.getOrBadRequest(taskParams().universeUUID);
    String targetUniverseMasterAddresses = targetUniverse.getMasterAddresses();
    String targetUniverseCertificate = targetUniverse.getCertificateNodetoNode();
    YBClient client = ybService.getClient(targetUniverseMasterAddresses, targetUniverseCertificate);

    try {
      GetMasterClusterConfigResponse resp = client.getMasterClusterConfig();
      if (resp.hasError()) {
        String errMsg =
            String.format(
                "Failed to sync XClusterConfigs for Universe(%s): "
                    + "Failed to get cluster config: %s",
                targetUniverse.universeUUID, resp.errorMessage());
        throw new RuntimeException(errMsg);
      }

      syncXClusterConfigs(resp.getConfig(), targetUniverse.universeUUID);

    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    } finally {
      ybService.closeClient(client, targetUniverseMasterAddresses);
    }

    log.info("Completed {}", getName());
  }

  private void syncXClusterConfigs(
      CatalogEntityInfo.SysClusterConfigEntryPB config, UUID targetUniverseUUID) {

    Set<Pair<UUID, String>> foundXClusterConfigs = new HashSet<>();

    Map<String, ProducerEntryPB> replicationGroups =
        config.getConsumerRegistry().getProducerMapMap();

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
          // Get the status of this replication group.
          XClusterConfigStatusType xClusterConfigStatus =
              value.getDisableStream()
                  ? XClusterConfigStatusType.Paused
                  : XClusterConfigStatusType.Running;
          log.info(
              "Found XClusterConfig({}) between source({}) and target({}): status({}), tables({})",
              xClusterConfigName,
              sourceUniverseUUID,
              targetUniverseUUID,
              xClusterConfigStatus,
              xClusterConfigTables);

          // Create or update a row in the Platform database for this replication group.
          XClusterConfig xClusterConfig =
              XClusterConfig.getByNameSourceTarget(
                  xClusterConfigName, sourceUniverseUUID, targetUniverseUUID);
          if (xClusterConfig == null) {
            XClusterConfigCreateFormData createFormData = new XClusterConfigCreateFormData();
            createFormData.name = xClusterConfigName;
            createFormData.sourceUniverseUUID = sourceUniverseUUID;
            createFormData.targetUniverseUUID = targetUniverseUUID;
            createFormData.tables = xClusterConfigTables;
            xClusterConfig = XClusterConfig.create(createFormData, xClusterConfigStatus);
            log.info("Created new XClusterConfig({})", xClusterConfig.uuid);
          } else {
            xClusterConfig.setTables(xClusterConfigTables);
            xClusterConfig.status = xClusterConfigStatus;
            xClusterConfig.update();
            log.info("Updated existing XClusterConfig({})", xClusterConfig.uuid);
          }
        });

    List<XClusterConfig> currentXClusterConfigsForTarget =
        XClusterConfig.getByTargetUniverseUUID(targetUniverseUUID);
    for (XClusterConfig xClusterConfig : currentXClusterConfigsForTarget) {
      if (!foundXClusterConfigs.contains(
          new Pair(xClusterConfig.sourceUniverseUUID, xClusterConfig.name))) {
        xClusterConfig.delete();
        log.info("Deleted unknown XClusterConfig({})", xClusterConfig.uuid);
      }
    }
  }
}
