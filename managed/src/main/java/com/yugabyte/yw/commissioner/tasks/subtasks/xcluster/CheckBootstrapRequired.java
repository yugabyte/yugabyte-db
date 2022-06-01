package com.yugabyte.yw.commissioner.tasks.subtasks.xcluster;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.forms.XClusterConfigTaskParams;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterTableConfig;
import java.util.ArrayList;
import java.util.List;
import java.util.Optional;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.IsBootstrapRequiredResponse;
import org.yb.client.YBClient;
import org.yb.client.YBTable;

@Slf4j
public class CheckBootstrapRequired extends XClusterConfigTaskBase {

  @Inject
  protected CheckBootstrapRequired(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends XClusterConfigTaskParams {
    // The source universe UUID must be stored in universeUUID field.
    // The parent xCluster config must be stored in xClusterConfig field.
    // Table id to check whether its any of its tables needs bootstrap.
    public String tableId;
    // Stream id for existing cdc streams to check whether it has fallen far behind.
    public String streamId;
  }

  @Override
  protected CheckBootstrapRequired.Params taskParams() {
    return (CheckBootstrapRequired.Params) taskParams;
  }

  @Override
  public String getName() {
    return String.format(
        "%s %s(sourceUniverse=%s, xClusterUuid=%s, tableId=%s, streamId=%s)",
        super.getName(),
        this.getClass().getSimpleName(),
        taskParams().universeUUID,
        taskParams().xClusterConfig.uuid,
        taskParams().tableId,
        taskParams().streamId);
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    // TODO: check what happens if taskParams().universeUUID is null.
    Universe sourceUniverse = Universe.getOrBadRequest(taskParams().universeUUID);
    String sourceUniverseMasterAddresses = sourceUniverse.getMasterAddresses();
    String sourceUniverseCertificate = sourceUniverse.getCertificateNodetoNode();
    YBClient client = ybService.getClient(sourceUniverseMasterAddresses, sourceUniverseCertificate);

    try {
      // Convert the table id to its tablet ids.
      YBTable ybTable = client.openTableByUUID(taskParams().tableId);
      List<String> tabletIds = new ArrayList<>(client.getTabletUUIDs(ybTable));
      // Each bootstrap task must belong to a parent xCluster config.
      if (taskParams().xClusterConfig == null) {
        throw new RuntimeException(
            "taskParams().xClusterConfig is null. Each CheckBootstrapRequired subtask must belong "
                + "to an xCluster config");
      }
      // Find the table config belonging to this subtask.
      Optional<XClusterConfig> xClusterConfig =
          XClusterConfig.maybeGet(taskParams().xClusterConfig.uuid);
      if (!xClusterConfig.isPresent()) {
        String errMsg =
            String.format(
                "No xCluster config with uuid (%s) found", taskParams().xClusterConfig.uuid);
        throw new RuntimeException(errMsg);
      }
      // Because in each xCluster config, there could be only one row for each table, this code
      // does not use streamId to find a table config.
      Optional<XClusterTableConfig> xClusterTableConfig =
          xClusterConfig.get().maybeGetTableById(taskParams().tableId);
      if (!xClusterTableConfig.isPresent()) {
        String errMsg =
            String.format(
                "Table config with table id (%s) does not belong to the "
                    + "parent xCluster config with uuid (%s)",
                taskParams().tableId, xClusterConfig.get().uuid);
        throw new RuntimeException(errMsg);
      }
      // Check whether bootstrap is required.
      IsBootstrapRequiredResponse resp =
          client.isBootstrapRequired(tabletIds, taskParams().streamId);
      if (resp.hasError()) {
        String errMsg =
            String.format(
                "Failed to do isBootstrapRequired universe (%s) for table (%s) and "
                    + "stream id (%s): %s",
                taskParams().universeUUID,
                taskParams().tableId,
                taskParams().streamId,
                resp.errorMessage());
        throw new RuntimeException(errMsg);
      }
      if (resp.bootstrapRequired()) {
        log.info(
            "Table {} with tablet ids {} and stream id {} needs bootstrap",
            taskParams().tableId,
            tabletIds,
            taskParams().streamId);
      }

      // Persist whether bootstrap is required.
      xClusterTableConfig.get().needBootstrap = resp.bootstrapRequired();
      xClusterTableConfig.get().update();

      if (HighAvailabilityConfig.get().isPresent()) {
        // We must increment version twice: one for openTableByUUID and one for isBootstrapRequired.
        getUniverse(true).incrementVersion();
        getUniverse(true).incrementVersion();
      }
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    } finally {
      ybService.closeClient(client, sourceUniverseMasterAddresses);
    }

    log.info("Completed {}", getName());
  }
}
