package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.XClusterReplicationTaskBase;
import com.yugabyte.yw.models.AsyncReplicationRelationship;
import com.yugabyte.yw.models.Universe;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.SetXClusterReplicationActiveResponse;
import org.yb.client.YBClient;

@Slf4j
public class XClusterReplicationSetActive extends XClusterReplicationTaskBase {

  @Inject
  protected XClusterReplicationSetActive(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    Universe targetUniverse = Universe.getOrBadRequest(taskParams().targetUniverseUUID);

    String masterHostPorts = targetUniverse.getMasterAddresses();
    String certificate = targetUniverse.getCertificateNodetoNode();
    YBClient client = null;

    try {
      client = ybService.getClient(masterHostPorts, certificate);
      SetXClusterReplicationActiveResponse resp =
          client.setXClusterReplicationActive(taskParams().sourceUniverseUUID, taskParams().active);

      if (resp.hasError()) {
        throw new RuntimeException(resp.errorMessage());
      }

      AsyncReplicationRelationship.getBetweenUniverses(
              taskParams().sourceUniverseUUID, taskParams().targetUniverseUUID)
          .forEach(relationship -> relationship.update(taskParams().active));
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    } finally {
      ybService.closeClient(client, masterHostPorts);
    }
  }
}
