package com.yugabyte.yw.commissioner.tasks.subtasks.xcluster;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.forms.XClusterConfigTaskParams;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterTableConfig;
import java.util.Objects;
import java.util.Set;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.DeleteCDCStreamResponse;
import org.yb.client.YBClient;

/**
 * It will remove all the bootstrap IDs created using {@link BootstrapProducer} task that could not
 * be used to set up replication. Because of that, the {@link DeleteReplication} task that deletes a
 * replication config will not delete them.
 */
@Slf4j
public class DeleteBootstrapIds extends XClusterConfigTaskBase {

  @Inject
  protected DeleteBootstrapIds(
      BaseTaskDependencies baseTaskDependencies, XClusterUniverseService xClusterUniverseService) {
    super(baseTaskDependencies, xClusterUniverseService);
  }

  public static class Params extends XClusterConfigTaskParams {
    // The source universe UUID must be stored in universeUUID field.
    // The parent xCluster config must be stored in xClusterConfig field.

    // The list of tables to remove the stream ids for. If null, it will be set to all tables in
    // the xCluster config.
    public Set<String> tableIds;

    // Whether the task must delete the bootstrap IDs even if they are in use.
    public boolean forceDelete;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public String getName() {
    return String.format(
        "%s(xClusterConfig=%s,forceDelete=%s)",
        super.getName(), taskParams().getXClusterConfig(), taskParams().forceDelete);
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    XClusterConfig xClusterConfig = getXClusterConfigFromTaskParams();

    if (Objects.isNull(taskParams().tableIds)) {
      throw new IllegalArgumentException("taskParams().tableIds could not be null");
    }
    if (xClusterConfig.getSourceUniverseUUID() == null) {
      log.info("Skipped {}: the source universe is destroyed", getName());
      return;
    }

    // Force delete when it is requested by the user or target universe is deleted.
    boolean forceDelete =
        taskParams().forceDelete || xClusterConfig.getTargetUniverseUUID() == null;

    // Get the bootstrap IDs to delete. Either the bootstrap flow had error, or the target universe
    // is deleted.
    Set<XClusterTableConfig> tableConfigsWithBootstrapId =
        xClusterConfig.getTableDetails().stream()
            .filter(
                tableConfig ->
                    taskParams().tableIds.contains(tableConfig.getTableId())
                        && tableConfig.getStreamId() != null)
            .collect(Collectors.toSet());
    Set<String> bootstrapIds =
        tableConfigsWithBootstrapId.stream()
            .map(XClusterTableConfig::getStreamId)
            .collect(Collectors.toSet());

    Universe sourceUniverse = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    String sourceUniverseMasterAddresses = sourceUniverse.getMasterAddresses();
    String sourceUniverseCertificate = sourceUniverse.getCertificateNodetoNode();
    if (bootstrapIds.isEmpty()) {
      log.info(
          "Skipped {}: There is no BootstrapId to delete for source universe ({})",
          getName(),
          sourceUniverse.getUniverseUUID());
      return;
    }
    log.info("Bootstrap ids to be deleted: {}", bootstrapIds);

    try (YBClient client =
        ybService.getClient(sourceUniverseMasterAddresses, sourceUniverseCertificate)) {
      // The `OBJECT_NOT_FOUND` error will be ignored.
      DeleteCDCStreamResponse resp =
          client.deleteCDCStream(bootstrapIds, true /* ignoreErrors */, forceDelete);
      if (resp.hasError()) {
        throw new RuntimeException(
            String.format(
                "Failed to delete bootstrapIds(%s) in XClusterConfig(%s): %s",
                bootstrapIds, xClusterConfig.getUuid(), resp.errorMessage()));
      }
      log.info(
          "BootstrapIds ({}) deleted from source universe ({})",
          bootstrapIds,
          sourceUniverse.getUniverseUUID());

      // Delete the bootstrap ids from DB.
      tableConfigsWithBootstrapId.forEach(
          tableConfig -> {
            tableConfig.setStreamId(null);
            tableConfig.setBootstrapCreateTime(null);
          });
      xClusterConfig.update();

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
