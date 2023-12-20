// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.commissioner.tasks.subtasks.xcluster;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import java.util.HashSet;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.SetupUniverseReplicationResponse;
import org.yb.client.YBClient;
import org.yb.util.NetUtil;

@Slf4j
public class XClusterConfigSetup extends XClusterConfigTaskBase {

  @Inject
  protected XClusterConfigSetup(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  public void initialize(ITaskParams params) {
    super.initialize(params);
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    XClusterConfig xClusterConfig = refreshXClusterConfig();
    Universe sourceUniverse = Universe.getOrBadRequest(xClusterConfig.sourceUniverseUUID);
    Universe targetUniverse = Universe.getOrBadRequest(xClusterConfig.targetUniverseUUID);

    String targetUniverseMasterAddresses = targetUniverse.getMasterAddresses();
    String targetUniverseCertificate = targetUniverse.getCertificateNodetoNode();
    YBClient client = ybService.getClient(targetUniverseMasterAddresses, targetUniverseCertificate);

    try {
      SetupUniverseReplicationResponse resp =
          client.setupUniverseReplication(
              xClusterConfig.getReplicationGroupName(),
              taskParams().createFormData.tables,
              // For dual NIC, the universes will be able to communicate over the secondary
              // addresses.
              new HashSet<>(
                  NetUtil.parseStringsAsPB(
                      sourceUniverse.getMasterAddresses(
                          false /* mastersQueryable */, true /* getSecondary */))));
      if (resp.hasError()) {
        throw new RuntimeException(
            String.format(
                "Failed to create XClusterConfig(%s): %s",
                xClusterConfig.uuid, resp.errorMessage()));
      }

      waitForXClusterOperation(client::isSetupUniverseReplicationDone);

      if (HighAvailabilityConfig.get().isPresent()) {
        getUniverse().incrementVersion();
      }

    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    } finally {
      ybService.closeClient(client, targetUniverseMasterAddresses);
    }

    log.info("Completed {}", getName());
  }
}
