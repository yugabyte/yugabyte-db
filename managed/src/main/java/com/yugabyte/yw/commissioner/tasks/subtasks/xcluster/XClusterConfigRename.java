// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.commissioner.tasks.subtasks.xcluster;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.AlterUniverseReplicationResponse;
import org.yb.client.YBClient;

@Slf4j
public class XClusterConfigRename extends XClusterConfigTaskBase {

  @Inject
  protected XClusterConfigRename(BaseTaskDependencies baseTaskDependencies) {
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
    Universe targetUniverse = Universe.getOrBadRequest(xClusterConfig.targetUniverseUUID);

    String targetUniverseMasterAddresses = targetUniverse.getMasterAddresses();
    String targetUniverseCertificate = targetUniverse.getCertificateNodetoNode();
    YBClient client = ybService.getClient(targetUniverseMasterAddresses, targetUniverseCertificate);

    try {
      String newName = taskParams().editFormData.name;

      log.info(
          "Renaming XClusterConfig({}): `{}` -> `{}`",
          xClusterConfig.uuid,
          xClusterConfig.name,
          newName);

      String newReplicationGroupName = xClusterConfig.sourceUniverseUUID + "_" + newName;

      AlterUniverseReplicationResponse resp =
          client.alterUniverseReplicationName(
              xClusterConfig.getReplicationGroupName(), newReplicationGroupName);
      if (resp.hasError()) {
        throw new RuntimeException(
            String.format(
                "Failed to rename XClusterConfig(%s): %s",
                xClusterConfig.uuid, resp.errorMessage()));
      }

      xClusterConfig.name = newName;
      xClusterConfig.update();

    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    } finally {
      ybService.closeClient(client, targetUniverseMasterAddresses);
    }

    log.info("Completed {}", getName());
  }
}
