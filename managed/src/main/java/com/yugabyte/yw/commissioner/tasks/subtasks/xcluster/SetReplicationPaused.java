package com.yugabyte.yw.commissioner.tasks.subtasks.xcluster;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.forms.XClusterConfigTaskParams;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.SetUniverseReplicationEnabledResponse;
import org.yb.client.YBClient;

@Slf4j
public class SetReplicationPaused extends XClusterConfigTaskBase {

  @Inject
  protected SetReplicationPaused(
      BaseTaskDependencies baseTaskDependencies, XClusterUniverseService xClusterUniverseService) {
    super(baseTaskDependencies, xClusterUniverseService);
  }

  public static class Params extends XClusterConfigTaskParams {
    // The target universe UUID must be stored in universeUUID field.
    // The parent xCluster config must be stored in xClusterConfig field.
    // Whether the replication should be paused.
    public boolean pause;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public String getName() {
    return String.format(
        "%s(xClusterConfig=%s,pause=%b)",
        super.getName(), taskParams().getXClusterConfig(), taskParams().pause);
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    XClusterConfig xClusterConfig = getXClusterConfigFromTaskParams();

    // Cannot pause a paused replication or resume an enabled replication.
    if (xClusterConfig.isPaused() == taskParams().pause) {
      if (xClusterConfig.isPaused()) {
        throw new RuntimeException(
            String.format("XClusterConfig %s is already paused", xClusterConfig));
      } else {
        throw new RuntimeException(
            String.format("XClusterConfig %s is already enabled", xClusterConfig));
      }
    }

    Universe targetUniverse = Universe.getOrBadRequest(xClusterConfig.getTargetUniverseUUID());
    String targetUniverseMasterAddresses = targetUniverse.getMasterAddresses();
    String targetUniverseCertificate = targetUniverse.getCertificateNodetoNode();
    try (YBClient client =
        ybService.getClient(targetUniverseMasterAddresses, targetUniverseCertificate)) {
      SetUniverseReplicationEnabledResponse resp =
          client.setUniverseReplicationEnabled(
              xClusterConfig.getReplicationGroupName(), !taskParams().pause /* active */);
      if (resp.hasError()) {
        throw new RuntimeException(
            String.format(
                "Failed to pause/enable XClusterConfig(%s): %s",
                xClusterConfig, resp.errorMessage()));
      }

      if (HighAvailabilityConfig.get().isPresent()) {
        getUniverse().incrementVersion();
      }

      // Save the pause state in the DB.
      xClusterConfig.updatePaused(taskParams().pause);
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    }

    log.info("Completed {}", getName());
  }
}
