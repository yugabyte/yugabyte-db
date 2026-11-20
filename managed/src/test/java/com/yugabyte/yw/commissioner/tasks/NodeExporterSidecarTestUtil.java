// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.common.KubernetesUtil;

/** Shared values for node_exporter sidecar tests (yb.kubernetes.node_exporter.enabled). */
public final class NodeExporterSidecarTestUtil {

  /** A DB version whose chart ships the sidecar. */
  public static final String SUPPORTED_VERSION =
      KubernetesUtil.MIN_VERSION_NODE_EXPORTER_SIDECAR_STABLE;

  private NodeExporterSidecarTestUtil() {}
}
