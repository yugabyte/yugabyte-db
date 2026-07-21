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
import com.fasterxml.jackson.annotation.JsonProperty;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.export.TelemetryConfig;
import com.yugabyte.yw.forms.VMImageUpgradeParams.VmUpgradeTaskType;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import com.yugabyte.yw.models.helpers.NodeStatus;
import com.yugabyte.yw.models.helpers.exporters.audit.AuditLogConfig;
import com.yugabyte.yw.models.helpers.exporters.metrics.MetricsExportConfig;
import com.yugabyte.yw.models.helpers.exporters.query.QueryLogConfig;
import com.yugabyte.yw.models.helpers.exporters.server.MasterLogConfig;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class AnsibleSetupServer extends NodeTaskBase {

  @Inject
  protected AnsibleSetupServer(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  // Additional parameters for this task.
  public static class Params extends NodeTaskParams {
    // The subnet into which the node's network interface needs to be provisioned.
    public String subnetId;

    // For AWS, this will dictate if we use the Time Sync Service.
    public boolean useTimeSync = false;

    public String machineImage;
    public UUID imageBundleUUID;

    // To use custom image flow if it is a VM upgrade with custom images.
    public VmUpgradeTaskType vmUpgradeTaskType = VmUpgradeTaskType.None;
    // For cron to systemd upgrades
    public boolean isSystemdUpgrade = false;
    // In case a node doesn't have custom AMI, ignore the value of USE_CUSTOM_IMAGE config.
    public boolean ignoreUseCustomImageConfig = false;
    // Amount of memory to limit the postgres process to via the ysql cgroup (in megabytes)
    public int cgroupSize = 0;
    // All telemetry export sections (audit/query/metrics/master) for the node, in one carrier.
    public TelemetryConfig telemetryConfig = null;

    @JsonIgnore
    public AuditLogConfig getAuditLogConfig() {
      return telemetryConfig != null ? telemetryConfig.getAuditLogConfig() : null;
    }

    @JsonIgnore
    public QueryLogConfig getQueryLogConfig() {
      return telemetryConfig != null ? telemetryConfig.getQueryLogConfig() : null;
    }

    @JsonIgnore
    public MetricsExportConfig getMetricsExportConfig() {
      return telemetryConfig != null ? telemetryConfig.getMetricsExportConfig() : null;
    }

    @JsonIgnore
    public MasterLogConfig getMasterLogConfig() {
      return telemetryConfig != null ? telemetryConfig.getMasterLogConfig() : null;
    }

    // Backward-compat: fold pre-migration top-level fields into telemetryConfig on deserialize.
    private TelemetryConfig ensureTelemetryConfig() {
      if (telemetryConfig == null) {
        telemetryConfig = new TelemetryConfig();
      }
      return telemetryConfig;
    }

    @JsonProperty("auditLogConfig")
    public void setLegacyAuditLogConfig(AuditLogConfig auditLogConfig) {
      ensureTelemetryConfig().setAuditLogConfig(auditLogConfig);
    }

    @JsonProperty("queryLogConfig")
    public void setLegacyQueryLogConfig(QueryLogConfig queryLogConfig) {
      ensureTelemetryConfig().setQueryLogConfig(queryLogConfig);
    }

    @JsonProperty("metricsExportConfig")
    public void setLegacyMetricsExportConfig(MetricsExportConfig metricsExportConfig) {
      ensureTelemetryConfig().setMetricsExportConfig(metricsExportConfig);
    }

    /*
     Reboot node for applying the ulimits, needed for 3 flows.
     1. Create Universe.
     2. Edit Cluster (Add Node).
     3. VM Image Upgrade.
    */
    public boolean rebootNodeAllowed = false;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    Provider p = taskParams().getProvider();
    boolean skipProvision = false;

    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    taskParams().useSystemd =
        universe.getUniverseDetails().getPrimaryCluster().userIntent.useSystemd;

    if (p.getCode().equals(Common.CloudType.onprem.name())) {
      skipProvision = p.getDetails().skipProvisioning;
    }

    if (skipProvision) {
      log.info("Skipping node provisioning");
    } else {
      // Execute the ansible command.
      getNodeManager()
          .nodeCommand(NodeManager.NodeCommandType.Provision, taskParams())
          .processErrors();
      setNodeStatus(NodeStatus.builder().nodeState(NodeState.ServerSetup).build());
    }
  }

  @Override
  public int getRetryLimit() {
    return 2;
  }
}
