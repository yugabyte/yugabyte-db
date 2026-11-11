// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundlev2.nodebin;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.ShellProcessContext;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.Optional;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;

/**
 * Uploads the yb-log-parse helper binary to DB nodes for support bundle scripts.
 *
 * <p>The binary replaces inline Python in {@code node_utils.sh} for deciding which log files fall
 * inside the collection window. It is uploaded rather than shipped with the node because DB nodes
 * are not redeployed for a YBA upgrade, and it is selected per node because a node's architecture
 * need not match the YBA host's.
 *
 * <p><b>Failure here is soft.</b> Every path returns {@code false} rather than throwing, and the
 * scripts fall back to their Python implementation -- slower, but correct. Contrast {@link
 * SupportBundleCdcStateDumpSupport}, whose binary has no fallback and whose caller treats the same
 * {@code false} as fatal for that component.
 */
@Slf4j
@Singleton
public class SupportBundleLogParseSupport {

  static final String LOG_PARSE_BINARY_NAME = "yb-log-parse";

  private final SupportBundleNodeBinarySupport binarySupport;

  @Inject
  SupportBundleLogParseSupport(SupportBundleNodeBinarySupport binarySupport) {
    this.binarySupport = binarySupport;
  }

  String remoteBinaryPath(NodeDetails node, Universe universe) {
    return binarySupport.remoteBinaryPath(LOG_PARSE_BINARY_NAME, node, universe);
  }

  /**
   * Uploads the helper when the node lacks it or holds a different build. Failure is soft: the
   * scripts fall back to their Python implementation, which is slower but correct.
   */
  public boolean ensureLogParseHelperOnNode(
      UUID bundleUuid, NodeDetails node, Universe universe, ShellProcessContext context) {
    return binarySupport.ensureBinaryOnNode(
        bundleUuid,
        LOG_PARSE_BINARY_NAME,
        node,
        universe,
        context,
        "scripts will fall back to Python",
        SupportBundleNodeBinarySupport.UnverifiedNodeCopy.TRUST);
  }

  Optional<String> detectNodeGoArch(
      NodeDetails node, Universe universe, ShellProcessContext context) {
    return binarySupport.detectNodeGoArch(node, universe, context);
  }

  static String archSpecificBinaryName(String goArch) {
    return SupportBundleNodeBinarySupport.archSpecificBinaryName(LOG_PARSE_BINARY_NAME, goArch);
  }
}
