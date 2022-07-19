// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.commissioner.tasks.subtasks.xcluster;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.forms.XClusterConfigTaskParams;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import java.util.List;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.WireProtocol;
import org.yb.client.DeleteUniverseReplicationResponse;
import org.yb.client.MasterErrorException;
import org.yb.client.YBClient;

@Slf4j
public class DeleteReplication extends XClusterConfigTaskBase {

  @Inject
  protected DeleteReplication(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
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
        super.getName(), taskParams().xClusterConfig, taskParams().ignoreErrors);
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    XClusterConfig xClusterConfig = getXClusterConfigFromTaskParams();

    if (xClusterConfig.targetUniverseUUID == null) {
      log.info("Skipped {}: the target universe is destroyed", getName());
      return;
    }

    // Ignore errors when it is requested by the user or source universe is deleted.
    boolean ignoreErrors = taskParams().ignoreErrors || xClusterConfig.sourceUniverseUUID == null;

    Universe targetUniverse = Universe.getOrBadRequest(xClusterConfig.targetUniverseUUID);
    String targetUniverseMasterAddresses = targetUniverse.getMasterAddresses();
    String targetUniverseCertificate = targetUniverse.getCertificateNodetoNode();
    try (YBClient client =
        ybService.getClient(targetUniverseMasterAddresses, targetUniverseCertificate)) {
      // Catch the `Universe replication NOT_FOUND` exception, and because it already does not
      // exist, the exception will be ignored.
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
                  xClusterConfig.uuid, resp.errorMessage()));
        }
      } catch (MasterErrorException e) {
        // If it is not `Universe replication NOT_FOUND` exception, rethrow the exception.
        if (!e.getMessage().contains("NOT_FOUND[code 1]: Universe replication")) {
          throw new RuntimeException(e);
        }
        log.warn(
            "XCluster config {} does not exist on the target universe, NOT_FOUND exception "
                + "occurred in deleteUniverseReplication RPC call is ignored: {}",
            xClusterConfig.uuid,
            e.getMessage());
      }

      // Set status of the xCluster config to `Deleted` if required.
      if (!isInMustDeleteStatus(xClusterConfig)) {
        xClusterConfig.setStatus(XClusterConfig.XClusterConfigStatusType.Deleted);
      }

      if (HighAvailabilityConfig.get().isPresent()) {
        getUniverse(true).incrementVersion();
      }
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    }

    log.info("Completed {}", getName());
  }
}
