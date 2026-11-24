// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundlev2.nodebin;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.ShellProcessContext;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.nio.file.Path;
import java.util.Optional;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;

/**
 * Deploys yb-dump-cdc-state to DB nodes for the collect_cdc_state support bundle component.
 *
 * <p>Deliberately shaped like {@link SupportBundleLogParseSupport} -- same upload-once-and-verify
 * flow, same per-node arch detection (reused from that class) -- with one difference that matters:
 * <b>there is no fallback</b>. Nothing else can dump {@code cdc_state}, so the caller turns a
 * {@code false} from {@link #ensureDumpBinaryOnNode} into a thrown exception that fails this
 * component, rather than degrading like the log-parse path does.
 */
@Slf4j
@Singleton
public class SupportBundleCdcStateDumpSupport {

  public static final String COLLECT_CDC_STATE_COMPONENT = "collect_cdc_state";
  public static final String BINARY_NAME = "yb-dump-cdc-state";

  private final SupportBundleNodeBinarySupport binarySupport;

  @Inject
  SupportBundleCdcStateDumpSupport(SupportBundleNodeBinarySupport binarySupport) {
    this.binarySupport = binarySupport;
  }

  /**
   * Scratch directory for the dump, derived from the tar path rather than fixed. The tar name
   * already carries the bundle UUID and node name, so deriving from it inherits that uniqueness and
   * two bundles collecting from the same node at once cannot write into each other's work dir.
   */
  public static String remoteWorkDirFromTarPath(String remoteTarPath) {
    if (remoteTarPath == null) {
      return null;
    }
    return remoteTarPath.replaceAll("\\.tar\\.gz$", "") + "-work";
  }

  public String remoteBinaryPath(NodeDetails node, Universe universe) {
    return binarySupport.remoteBinaryPath(BINARY_NAME, node, universe);
  }

  /**
   * Uploads the dump binary when the node lacks it or holds a different build. Unlike the log-parse
   * helper there is no fallback, so the caller turns a false here into a failed component.
   */
  public boolean ensureDumpBinaryOnNode(
      UUID bundleUuid, NodeDetails node, Universe universe, ShellProcessContext context) {
    return binarySupport.ensureBinaryOnNode(
        bundleUuid,
        BINARY_NAME,
        node,
        universe,
        context,
        "collect_cdc_state will fail",
        SupportBundleNodeBinarySupport.UnverifiedNodeCopy.TRUST);
  }

  static String archSpecificBinaryName(String goArch) {
    return SupportBundleNodeBinarySupport.archSpecificBinaryName(BINARY_NAME, goArch);
  }

  Optional<Path> resolveLocalBinaryPath(Optional<String> goArch) {
    return binarySupport.resolveLocalBinaryPath(BINARY_NAME, goArch);
  }
}
