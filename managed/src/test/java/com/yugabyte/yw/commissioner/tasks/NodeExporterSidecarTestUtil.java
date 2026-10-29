// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks;

/** Shared values for node_exporter sidecar tests (yb.kubernetes.node_exporter.enabled). */
public final class NodeExporterSidecarTestUtil {

  /** A DB version whose chart ships the sidecar. */
  public static final String SUPPORTED_VERSION = "2026.2.0.0-b1";

  private NodeExporterSidecarTestUtil() {}
}
