/*
 * Copyright 2019 YugabyteDB, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.commissioner.tasks.params.ServerSubTaskParams;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.audit.otel.OtelCollectorUtil;
import com.yugabyte.yw.common.export.TelemetryConfig;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.HashMap;
import java.util.Map;
import java.util.Map.Entry;
import java.util.function.Supplier;
import javax.annotation.Nullable;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import org.yb.client.YBClientApi;

@Slf4j
public class SetFlagInMemory extends ServerSubTaskBase {

  // The gflag for tserver addresses.
  private static final String TSERVER_MASTER_ADDR_FLAG = "tserver_master_addrs";

  // The gflag for master addresses.
  private static final String MASTER_MASTER_ADDR_FLAG = "master_addresses";

  @Inject
  public SetFlagInMemory(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  // Parameters for wait task.
  public static class Params extends ServerSubTaskParams {
    // If the flags need to be forced to update (if it is not marked runtime safe).
    public boolean force = false;

    // The set of flags that need to be updated.
    public Map<String, String> gflags;

    // If ONLY the masters need to be updated.
    public boolean updateMasterAddrs = false;
    // Supplier for master addresses override which is invoked only when the subtask starts
    // execution.
    @Nullable public Supplier<String> masterAddrsOverride;

    @JsonIgnore
    public String getMasterAddrsOverride() {
      String masterAddresses = masterAddrsOverride == null ? null : masterAddrsOverride.get();
      if (StringUtils.isNotBlank(masterAddresses)) {
        log.info("Using the master addresses {} from the override", masterAddresses);
      }
      return masterAddresses;
    }
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    checkParams();
    // Check if secondary IP is present, and if so, use that for the gflag value.
    boolean isTserverTask = taskParams().serverType == ServerType.TSERVER;
    HostAndPort hp = getHostPort();

    Map<String, String> gflags;
    if (taskParams().updateMasterAddrs) {
      String masterAddresses = taskParams().getMasterAddrsOverride();
      if (StringUtils.isBlank(masterAddresses)) {
        masterAddresses = getMasterAddresses(true);
      }
      String flagToSet = isTserverTask ? TSERVER_MASTER_ADDR_FLAG : MASTER_MASTER_ADDR_FLAG;
      gflags = new HashMap<>(Map.of(flagToSet, masterAddresses));
    } else if (taskParams().gflags == null) {
      throw new IllegalArgumentException("Gflags cannot be null during a setFlag operation.");
    } else {
      gflags = new HashMap<>(taskParams().gflags);
    }
    try (YBClientApi client = getClient()) {
      Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());

      if (!taskParams().updateMasterAddrs) {
        String userProvidedYsqlPgConfCsv = gflags.getOrDefault(GFlagsUtil.YSQL_PG_CONF_CSV, "");
        // Read telemetry from the same source as the server's config: the VM config file has it
        // only when the node's cluster has YSQL enabled, K8s helm overrides use only the
        // export_telemetry_config row and only for tservers.
        TelemetryConfig telemetryConfig = null;
        if (!Util.isKubernetesBasedUniverse(universe)) {
          NodeDetails node = universe.getNode(taskParams().nodeName);
          Cluster cluster = node == null ? null : universe.getCluster(node.placementUuid);
          if (cluster == null) {
            cluster = universe.getUniverseDetails().getPrimaryCluster();
          }
          if (cluster.userIntent.enableYSQL) {
            telemetryConfig = OtelCollectorUtil.getCurrentTelemetryConfig(universe);
          }
        } else if (taskParams().serverType == ServerType.TSERVER) {
          telemetryConfig =
              OtelCollectorUtil.getPersistedTelemetryConfig(universe.getUniverseUUID());
        }
        String telemetryYsqlPgConfCsv =
            telemetryConfig == null
                ? ""
                : GFlagsUtil.getYsqlPgConfCsv(
                    telemetryConfig.getAuditLogConfig(), telemetryConfig.getQueryLogConfig());
        // Build the value in the same order as the server's gflags file (user, then new install
        // gflags where they are applied, then telemetry over both): GFlagsUpgrade compares the
        // in-memory string with the target.
        String baseYsqlPgConfCsv = userProvidedYsqlPgConfCsv;
        if (GFlagsUtil.areNewInstallGFlagsApplied(universe)) {
          baseYsqlPgConfCsv =
              GFlagsUtil.mergeCSVs(
                  baseYsqlPgConfCsv,
                  universe
                      .getNewInstallGFlags(taskParams().serverType)
                      .getOrDefault(GFlagsUtil.YSQL_PG_CONF_CSV, ""),
                  true);
        }
        if (StringUtils.isNotBlank(baseYsqlPgConfCsv)
            || StringUtils.isNotBlank(telemetryYsqlPgConfCsv)) {
          // K8s helm overrides pass the user value through unchanged unless the export row has an
          // audit or query section (KubernetesCommandExecutor checks requiresYsqlPgConfCsv).
          boolean keepRaw =
              Util.isKubernetesBasedUniverse(universe)
                  && (telemetryConfig == null || !telemetryConfig.requiresYsqlPgConfCsv());
          gflags.put(
              GFlagsUtil.YSQL_PG_CONF_CSV,
              keepRaw
                  ? baseYsqlPgConfCsv
                  : GFlagsUtil.overrideWithTelemetryYsqlPgConfCsv(
                      baseYsqlPgConfCsv, telemetryYsqlPgConfCsv));
        }
      }
      if (universe.getUniverseDetails().getPrimaryCluster().userIntent.enableConnectionPooling
          && gflags.containsKey(GFlagsUtil.PSQL_PROXY_BIND_ADDRESS)) {
        String psqlProxyBindAddress = gflags.get(GFlagsUtil.PSQL_PROXY_BIND_ADDRESS);
        // Remove the port from the psql proxy bind address after the colon and add the internal PG
        // process port.
        String psqlProxyBindAddressWithoutPort =
            psqlProxyBindAddress.substring(0, psqlProxyBindAddress.lastIndexOf(":"));
        String ysqlConnMgrPort =
            String.valueOf(
                universe.getUniverseDetails().communicationPorts.internalYsqlServerRpcPort);
        gflags.put(
            GFlagsUtil.PSQL_PROXY_BIND_ADDRESS,
            psqlProxyBindAddressWithoutPort + ":" + ysqlConnMgrPort);
      }
      // allowed_preview_flags_csv should be set first in order to set the preview flags.
      if (gflags.containsKey(GFlagsUtil.ALLOWED_PREVIEW_FLAGS_CSV)) {
        log.info(
            "Setting Allowed Preview Flags for {} on node {}",
            taskParams().serverType,
            taskParams().nodeName);
        setFlag(
            client,
            GFlagsUtil.ALLOWED_PREVIEW_FLAGS_CSV,
            gflags.get(GFlagsUtil.ALLOWED_PREVIEW_FLAGS_CSV),
            hp);
        gflags.remove(GFlagsUtil.ALLOWED_PREVIEW_FLAGS_CSV);
        log.info(
            "Setting remaining flags for {} on node {}",
            taskParams().serverType,
            taskParams().nodeName);
      }
      for (Entry<String, String> gflag : gflags.entrySet()) {
        setFlag(client, gflag.getKey(), gflag.getValue(), hp);
      }
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    }
  }

  private void setFlag(YBClientApi client, String gflag, String value, HostAndPort hp)
      throws Exception {
    log.debug("Setting gflag {} to {} on node {} via non-restart rpc", gflag, value, hp);
    boolean setSuccess = client.setFlag(hp, gflag, value, taskParams().force);
    if (!setSuccess) {
      throw new RuntimeException(
          "Could not set gflag "
              + gflag
              + " for "
              + taskParams().serverType
              + " on node "
              + taskParams().nodeName);
    }
  }
}
