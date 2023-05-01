/*
 * Copyright 2019 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks.subtasks.xcluster;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.forms.XClusterConfigTaskParams;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.CommonNet;
import org.yb.cdc.CdcConsumer;
import org.yb.client.AlterUniverseReplicationResponse;
import org.yb.client.GetMasterClusterConfigResponse;
import org.yb.client.YBClient;

@Slf4j
public class XClusterConfigUpdateMasterAddresses extends XClusterConfigTaskBase {

  @Inject
  protected XClusterConfigUpdateMasterAddresses(
      BaseTaskDependencies baseTaskDependencies, XClusterUniverseService xClusterUniverseService) {
    super(baseTaskDependencies, xClusterUniverseService);
  }

  public static class Params extends XClusterConfigTaskParams {
    // The target universe UUID must be stored in universeUUID field.
    // Source universe UUID.
    public UUID sourceUniverseUuid;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public String getName() {
    return String.format(
        "%s (targetUniverse=%s, sourceUniverse=%s)",
        super.getName(), taskParams().getUniverseUUID(), taskParams().sourceUniverseUuid);
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    checkUniverseVersion();
    Universe targetUniverse = lockUniverse(-1 /* expectedUniverseVersion */);
    String targetUniverseMasterAddresses = targetUniverse.getMasterAddresses();
    String targetUniverseCertificate = targetUniverse.getCertificateNodetoNode();
    try (YBClient client =
        ybService.getClient(targetUniverseMasterAddresses, targetUniverseCertificate)) {
      GetMasterClusterConfigResponse getMasterClusterConfigResp = client.getMasterClusterConfig();
      if (getMasterClusterConfigResp.hasError()) {
        String errMsg =
            String.format(
                "Failed to update master addresses of XClusterConfigs for Universe(%s): "
                    + "Failed to get cluster config: %s",
                targetUniverse.getUniverseUUID(), getMasterClusterConfigResp.errorMessage());
        throw new RuntimeException(errMsg);
      }
      Map<String, CdcConsumer.ProducerEntryPB> replicationGroups =
          getMasterClusterConfigResp.getConfig().getConsumerRegistry().getProducerMapMap();

      // Update all the xCluster configs whose source and target universes belong to this task.
      for (String replicationGroupName : replicationGroups.keySet()) {
        XClusterConfig xClusterConfig =
            XClusterConfig.getByReplicationGroupNameTarget(
                replicationGroupName, targetUniverse.getUniverseUUID());
        if (xClusterConfig == null) {
          // Skip replication for xcluster replications were created by yb-admin command and not
          // existing in YBA.
          continue;
        }
        UUID sourceUniverseUUID = xClusterConfig.getSourceUniverseUUID();
        String xClusterConfigName = xClusterConfig.getName();
        // Skip the replication configs whose source universe does not belong to this task.
        if (!sourceUniverseUUID.equals(taskParams().sourceUniverseUuid)) {
          continue;
        }
        // Get the master addresses from the source universe.
        Universe sourceUniverse = Universe.getOrBadRequest(sourceUniverseUUID);
        Set<CommonNet.HostPortPB> sourceMasterAddresses = new HashSet<>();
        List<NodeDetails> sourceMasters = sourceUniverse.getMasters();
        for (NodeDetails node : sourceMasters) {
          CommonNet.HostPortPB hostPortPB =
              CommonNet.HostPortPB.newBuilder()
                  .setHost(node.cloudInfo.private_ip)
                  .setPort(node.masterRpcPort)
                  .build();
          sourceMasterAddresses.add(hostPortPB);
        }
        // Update the replication config on the target universe with the new source master
        // addresses.
        AlterUniverseReplicationResponse resp =
            client.alterUniverseReplicationSourceMasterAddresses(
                replicationGroupName, sourceMasterAddresses);
        if (resp.hasError()) {
          String errMsg =
              String.format(
                  "Failed to update source master addresses for XClusterConfig(%s) "
                      + "between source(%s) and target(%s) to %s: %s",
                  xClusterConfigName,
                  sourceUniverseUUID,
                  taskParams().getUniverseUUID(),
                  sourceUniverse.getMasterAddresses(),
                  resp.errorMessage());
          throw new RuntimeException(errMsg);
        }
        log.info(
            "Master addresses for XClusterConfig({}) between source({}) and target({}) "
                + "updated to {}",
            xClusterConfigName,
            sourceUniverseUUID,
            taskParams().getUniverseUUID(),
            sourceUniverse.getMasterAddresses());

        if (HighAvailabilityConfig.get().isPresent()) {
          getUniverse(true).incrementVersion();
        }
      }
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    } finally {
      unlockUniverseForUpdate();
    }

    log.info("Completed {}", getName());
  }
}
