// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundlev2;

import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.ShellProcessContext;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.List;
import java.util.Optional;
import lombok.extern.slf4j.Slf4j;

/**
 * Reads the checksum of a file already on a node, so callers can tell a stale copy from a current
 * one instead of re-uploading blindly.
 *
 * <p>The command is plain argv and the output is split here. The obvious shell form -- {@code
 * md5sum 'path' | awk '{print $1}'} -- is exactly what this replaces: support bundle commands go
 * out with {@code useBash=false}, which means the argv is an exact command line that no shell
 * re-interprets, so the pipe and the quotes reached {@code md5sum} as literal arguments and the
 * call produced nothing. A checksum that is always empty never matches, so every caller re-uploaded
 * on every call while logging that it had compared checksums.
 */
@Slf4j
public final class SupportBundleRemoteChecksum {

  private SupportBundleRemoteChecksum() {}

  /**
   * Returns the md5 of {@code remotePath} on the node, or empty when it cannot be established.
   *
   * <p>Empty means "unknown", never "unchanged": a missing file, an unreadable one and a transport
   * failure are indistinguishable from here, and the safe reading of all three is that the node's
   * copy cannot be trusted.
   */
  public static Optional<String> remoteMd5(
      NodeUniverseManager nodeUniverseManager,
      NodeDetails node,
      Universe universe,
      String remotePath,
      ShellProcessContext context) {
    try {
      ShellResponse response =
          nodeUniverseManager.runCommand(
              node, universe, List.of("md5sum", remotePath), context, false /* useBash */);
      SupportBundleCancellation.throwIfCancelled(response);
      if (!response.isSuccess()) {
        return Optional.empty();
      }
      // Not extractRunCommandOutput() unguarded: it throws when the prefix is absent, and only the
      // run_node_action transport adds one. Swallowing that would put us back to an always-empty
      // checksum on the node agent, which is the failure this replaces.
      String message = response.getMessage() == null ? "" : response.getMessage();
      String output =
          message.contains(ShellResponse.RUN_COMMAND_OUTPUT_PREFIX)
              ? response.extractRunCommandOutput().trim()
              : message.trim();
      // md5sum prints "<checksum>  <path>"; a file it cannot read is a non-zero exit, not an empty
      // line, so the missing-file case needs no separate guard.
      if (output.isEmpty()) {
        return Optional.empty();
      }
      return Optional.of(output.split("\\s+", 2)[0]);
    } catch (Exception e) {
      SupportBundleCancellation.rethrowIfCancelled(e);
      log.debug("Could not read checksum of {} on node {}", remotePath, node.getNodeName(), e);
      return Optional.empty();
    }
  }
}
