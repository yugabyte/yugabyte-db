package com.yugabyte.yw.models.helpers.telemetry;

public enum ExportType {
  // Audit and query logs are emitted via PG-side gflags the tserver reads only at startup, so
  // changing them requires a DB process restart.
  AUDIT_LOGS(true),
  QUERY_LOGS(true),
  // No DB restart needed.
  METRICS(false),
  MASTER_LOGS(false);

  private final boolean requiresDbRestart;

  ExportType(boolean requiresDbRestart) {
    this.requiresDbRestart = requiresDbRestart;
  }

  /** Whether changing this section requires restarting the yb-master/yb-tserver processes. */
  public boolean requiresDbRestart() {
    return requiresDbRestart;
  }
}
