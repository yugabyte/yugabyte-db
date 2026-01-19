package com.yugabyte.yw.commissioner.tasks.subtasks;

import static com.yugabyte.yw.models.helpers.CustomerConfigConsts.NAME_NFS;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.customer.config.CustomerConfigService;
import com.yugabyte.yw.controllers.handlers.UniverseTableHandler;
import com.yugabyte.yw.forms.AbstractTaskParams;
import com.yugabyte.yw.forms.BackupTableParams;
import com.yugabyte.yw.forms.TableInfoForm.TableInfoResp;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageNFSData;
import com.yugabyte.yw.models.configs.data.CustomerConfigStorageNFSData.RegionLocations;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.List;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class BackupPreflightValidate extends AbstractTaskBase {

  private final CustomerConfigService configService;
  private final UniverseTableHandler tableHandler;
  private final String FREE_SPACE_CMD = "df -P %s | tail -n1 ";
  private final String PRECHECK_FAILED_MSG = "NFS space precheck failed. ";

  public static class Params extends AbstractTaskParams {
    public Params(UUID storageConfigUUID, UUID customerUUID, UUID universeUUID, boolean ybcBackup) {
      this.storageConfigUUID = storageConfigUUID;
      this.customerUUID = customerUUID;
      this.universeUUID = universeUUID;
      this.ybcBackup = ybcBackup;
    }

    public Params(BackupTableParams backupTableParams, boolean ybcBackup) {
      this.backupTableParams = backupTableParams;
      this.ybcBackup = ybcBackup;
      this.storageConfigUUID = backupTableParams.storageConfigUUID;
      this.customerUUID = backupTableParams.customerUuid;
      this.universeUUID = backupTableParams.getUniverseUUID();
    }

    public UUID storageConfigUUID;
    public UUID customerUUID;
    public UUID universeUUID;
    public boolean ybcBackup;
    public BackupTableParams backupTableParams;
  }

  @Override
  public BackupPreflightValidate.Params taskParams() {
    return (BackupPreflightValidate.Params) taskParams;
  }

  @Inject
  public BackupPreflightValidate(
      BaseTaskDependencies baseTaskDependencies,
      CustomerConfigService configService,
      UniverseTableHandler tableHandler) {
    super(baseTaskDependencies);
    this.configService = configService;
    this.tableHandler = tableHandler;
  }

  @Override
  public void run() {
    if (taskParams().ybcBackup) {
      CustomerConfig storageConfig =
          configService.getOrBadRequest(taskParams().customerUUID, taskParams().storageConfigUUID);
      Universe universe = Universe.getOrBadRequest(taskParams().universeUUID);
      backupHelper.validateStorageConfigForBackupOnUniverse(storageConfig, universe);

      if (confGetter.getConfForScope(universe, UniverseConfKeys.enableNfsBackupPrecheck)) {
        try {
          doNfsSpacePrecheck(storageConfig, universe);
        } catch (Exception e) {
          // Only throw if the space precheck fails,
          // log error and continue backup otherwise
          if (e.getMessage().contains(PRECHECK_FAILED_MSG)) {
            throw e;
          } else {
            log.error("Error while running NFS precheck on universe: {}", universe.getName());
            e.printStackTrace();
          }
        }
      }
    }
  }

  private void doNfsSpacePrecheck(CustomerConfig storageConfig, Universe universe) {
    BackupTableParams tableParams = taskParams().backupTableParams;
    // validate storage space on NFS mount for full backups based on SST and WAL size.
    if (storageConfig.getName().equals(NAME_NFS)
        && tableParams != null
        && tableParams.baseBackupUUID.equals(tableParams.backupUuid)) {
      log.info("Starting NFS storage precheck for config: " + storageConfig.getConfigName());
      List<TableInfoResp> tableInfos =
          tableHandler.listTables(
              taskParams().customerUUID,
              universe.getUniverseUUID(),
              false /* includeParentTableInfo */,
              false /* excludeColocatedTables */,
              false /* includeColocatedParentTables */,
              false /* xClusterSupportedOnly */);
      if (tableInfos.isEmpty()) {
        log.warn(
            "Skipping NFS precheck since we are unable to get table info for universe: "
                + universe.getName());
        return;
      }
      CustomerConfigStorageNFSData configData =
          (CustomerConfigStorageNFSData) storageConfig.getDataObject();

      // Calculate total size in bytes based on table params
      double totalSize = 0;
      for (BackupTableParams params : tableParams.backupList) {
        // Account for all tables in the keyspace
        double requiredSpace;
        if (params.allTables) {
          requiredSpace =
              tableInfos.stream()
                  .filter(info -> info.keySpace.equals(params.getKeyspace()))
                  .mapToDouble(info -> info.walSizeBytes + info.sizeBytes)
                  .sum();
        }
        // Only count the tables in the given list.
        else {
          requiredSpace =
              tableInfos.stream()
                  .filter(info -> params.tableNameList.contains(info.tableName))
                  .mapToDouble(info -> info.walSizeBytes + info.sizeBytes)
                  .sum();
        }
        log.debug(
            "Required space for keyspace {} = {}MB",
            params.getKeyspace(),
            requiredSpace / (1024 * 1024));
        totalSize += requiredSpace;
      }
      final long RF =
          universe.getUniverseDetails().getPrimaryCluster().userIntent.replicationFactor;
      // Bytes to KB.
      totalSize /= 1024;
      // We only backup the tablet leaders so need to divide by RF + some buffer.
      long spaceNeeded =
          (long) totalSize / RF
              + confGetter.getConfForScope(universe, UniverseConfKeys.nfsPrecheckBufferSpace);
      log.debug("Total space needed = {}MB.", spaceNeeded / 1024);

      //  Check available space on one tserver.
      if (configData.regionLocations == null) {
        NodeDetails tserver =
            universe.getTServersInPrimaryCluster().stream()
                .filter(node -> nodeUniverseManager.isNodeReachable(node, universe, 30))
                .findAny()
                .get();
        tserverSpaceCheck(tserver, spaceNeeded, configData.backupLocation, universe);
      } else {
        UserIntent intent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
        int numRegions = intent.regionList.size();
        String preferredRegion =
            intent.preferredRegion != null
                ? Region.getOrBadRequest(intent.preferredRegion).getCode()
                : "";
        // Check available space on one tserver from each given region.
        for (RegionLocations region : configData.regionLocations) {
          var tserver =
              universe.getTServers().stream()
                  .filter(node -> node.getRegion().equals(region.region))
                  .filter(node -> nodeUniverseManager.isNodeReachable(node, universe, 30))
                  .findAny();
          if (tserver.isPresent()) {
            tserverSpaceCheck(
                tserver.get(),
                // Incase of preferred region we check for full space
                region.region.equals(preferredRegion)
                    ? spaceNeeded
                    : spaceNeeded / numRegions /* space needed in each region */,
                region.location,
                universe);
          } else {
            log.warn(
                "Skipping NFS precheck in region {} since no reachable tserver found.",
                region.region);
          }
        }
      }
    }
  }

  private void tserverSpaceCheck(
      NodeDetails tserver, long spaceNeeded, String location, Universe universe) {
    // Should return the free disk space in the given location in KBs.
    ShellResponse resp =
        nodeUniverseManager
            .runCommand(
                tserver, universe, List.of("bash", "-c", String.format(FREE_SPACE_CMD, location)))
            .processErrors("Error while finding free space on " + tserver.nodeName);
    log.debug("Response for free space cmd on node {} = {}", tserver.nodeName, resp.toString());
    // Resp.message looks like: "Command output:/dev/sda1  47227284 16333704 30877196 35%"
    var respSplit =
        List.of(
            resp.getMessage().substring(resp.getMessage().indexOf(":") + 1).strip().split("\\s+"));
    long spaceAvailable = Long.parseLong(respSplit.get(3).trim());
    log.debug(
        "Space available on path {} on node {} = {}MB.",
        location,
        tserver.nodeName,
        spaceAvailable / 1024);
    if (spaceAvailable < spaceNeeded) {
      throw new RuntimeException(
          String.format(
              PRECHECK_FAILED_MSG
                  + "Need atleast %dMB but only %dMB present. Set"
                  + " 'yb.backup.enableNfsPrecheck' to false to disable this check.",
              spaceNeeded / 1024,
              spaceAvailable / 1024));
    }
  }
}
