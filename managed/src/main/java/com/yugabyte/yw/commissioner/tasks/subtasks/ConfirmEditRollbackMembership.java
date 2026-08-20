// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.ServerSubTaskParams;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.HashSet;
import java.util.Set;
import java.util.stream.Collectors;
import java.util.stream.Stream;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import org.yb.client.ListLiveTabletServersResponse;
import org.yb.client.ListMasterRaftPeersResponse;
import org.yb.client.YBClientApi;
import play.libs.Json;

/**
 * After edit-rollback destroy/blacklist cleanup, verify the master only reports servers that are
 * Live-before or were ADD nodes being destroyed (transient leftovers). Unexpected extras indicate
 * the cluster membership does not match the topology we are about to restore.
 */
@Slf4j
public class ConfirmEditRollbackMembership extends ServerSubTaskBase {

  @Inject
  protected ConfirmEditRollbackMembership(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends ServerSubTaskParams {
    public UniverseDefinitionTaskParams beforeUniverseDetails;

    /** Private IPs of ADD nodes destroyed during rollback (captured before delete from YBA). */
    public Set<String> destroyedNodeIps = new HashSet<>();
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    UniverseDefinitionTaskParams before = taskParams().beforeUniverseDetails;
    if (before == null || before.nodeDetailsSet == null) {
      log.warn("No before details for membership check; skipping");
      return;
    }
    Set<String> allowedIps = new HashSet<>();
    for (NodeDetails node : before.nodeDetailsSet) {
      if (node.state != NodeDetails.NodeState.Live) {
        continue;
      }
      if (node.cloudInfo != null && StringUtils.isNotBlank(node.cloudInfo.private_ip)) {
        allowedIps.add(node.cloudInfo.private_ip);
      }
    }
    // Master may still briefly report ADD IPs after destroy; allow those captured at enqueue time.
    if (taskParams().destroyedNodeIps != null) {
      allowedIps.addAll(taskParams().destroyedNodeIps);
    }

    try (YBClientApi client = ybService.getUniverseClient(universe)) {
      Set<String> reported = new HashSet<>();
      ListMasterRaftPeersResponse masters = client.listMasterRaftPeers();
      if (masters != null && masters.getPeersList() != null) {
        masters
            .getPeersList()
            .forEach(
                p ->
                    Stream.concat(
                            p.getLastKnownPrivateIps().stream(),
                            p.getLastKnownBroadcastIps().stream())
                        .map(hp -> hp.getHost())
                        .filter(StringUtils::isNotBlank)
                        .forEach(reported::add));
      }
      ListLiveTabletServersResponse tservers = client.listLiveTabletServers();
      if (tservers != null && tservers.getTabletServers() != null) {
        tservers.getTabletServers().stream()
            .map(ts -> ts.getPrivateAddress().getHost())
            .filter(StringUtils::isNotBlank)
            .forEach(reported::add);
      }

      log.debug(
          "Rollback membership check for universe {}: reported={}, allowed={}",
          universe.getUniverseUUID(),
          Json.toJson(reported),
          Json.toJson(allowedIps));
      Set<String> unexpected =
          reported.stream().filter(ip -> !allowedIps.contains(ip)).collect(Collectors.toSet());
      if (!unexpected.isEmpty()) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            "Cannot roll back edit universe: master reports unexpected servers "
                + Json.toJson(unexpected)
                + " not in Live-before topology (allowed="
                + Json.toJson(allowedIps)
                + ")");
      }
      log.info(
          "Rollback membership check ok for universe {}: {} reported IP(s) within allowed set",
          universe.getUniverseUUID(),
          reported.size());
    } catch (PlatformServiceException e) {
      throw e;
    } catch (Exception e) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Cannot roll back edit universe: failed to verify master membership - " + e.getMessage());
    }
  }
}
