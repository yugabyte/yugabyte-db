package com.yugabyte.yw.models.helpers.telemetry;

public enum ExportType {
  // Constructor args: (requiresDbRestart, supportedOnKubernetes).
  //
  // requiresDbRestart: audit and query logs are emitted via PG-side gflags the tserver reads only
  // at startup, so changing them requires a DB process restart; everything else is reconfigured by
  // the otel collector without bouncing the DB processes.
  //
  // supportedOnKubernetes: whether this export type's source is reachable by the collector on K8s,
  // where the collector runs as a sidecar in the yb-master/yb-tserver pods and can only tail
  // pod-local files. Node-level sources (node-agent, YNP) do not exist inside the pods, so they are
  // VM-only; every other source is either pod-local or scraped pod-locally. This gates export on
  // Kubernetes in ExportTelemetryConfigParams. (A separate, finer chart-version check -
  // requirePassthroughForK8s - decides whether a K8s-supported type is renderable on the universe's
  // current Helm chart.)
  AUDIT_LOGS(true, true),
  QUERY_LOGS(true, true),
  METRICS(false, true),
  MASTER_LOGS(false, true),
  TSERVER_LOGS(false, true),
  // Internal-only diagnostic log sources (plain file-tail exports, no gflag, no DB restart).
  YSQL_CONN_MGR_LOGS(false, true), // co-located with postgres logs in the yb-tserver pod
  NODE_AGENT_LOGS(false, false), // node-agent does not run inside K8s pods
  YNP_LOGS(false, false), // node provisioning is a VM/on-prem concept
  CONTROLLER_LOGS(false, true); // yb-controller sidecar in the yb-tserver pod

  private final boolean requiresDbRestart;
  private final boolean supportedOnKubernetes;

  ExportType(boolean requiresDbRestart, boolean supportedOnKubernetes) {
    this.requiresDbRestart = requiresDbRestart;
    this.supportedOnKubernetes = supportedOnKubernetes;
  }

  /** Whether changing this section requires restarting the yb-master/yb-tserver processes. */
  public boolean requiresDbRestart() {
    return requiresDbRestart;
  }

  /**
   * Whether this export type is available on Kubernetes universes at all (its source exists inside
   * the yb-master/yb-tserver pods for the collector sidecar to reach). Types that return false are
   * VM-only and must be rejected when requested on a K8s universe.
   */
  public boolean isSupportedOnKubernetes() {
    return supportedOnKubernetes;
  }
}
