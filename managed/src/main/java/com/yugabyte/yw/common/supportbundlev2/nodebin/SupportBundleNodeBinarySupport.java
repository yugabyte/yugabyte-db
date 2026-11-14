// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundlev2.nodebin;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.AppConfigHelper;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.ShellProcessContext;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.supportbundlev2.SupportBundleCancellation;
import com.yugabyte.yw.common.supportbundlev2.SupportBundleNodeStaging;
import com.yugabyte.yw.common.supportbundlev2.SupportBundleRemoteChecksum;
import com.yugabyte.yw.common.utils.FileUtils;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Arrays;
import java.util.Optional;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

/**
 * Deploys the support bundle helper binaries to DB nodes and keeps them current.
 *
 * <p>Three helpers are pushed this way -- yb-log-parse, yb-redact-server-conf and
 * yb-dump-cdc-state. They are uploaded rather than shipped with the node because DB nodes are not
 * redeployed for a YBA upgrade, and selected per node because a node's architecture need not match
 * the YBA host's.
 *
 * <p><b>Presence is not enough; the copy on the node must match YBA's.</b> Comparing checksums
 * rather than testing for existence is what lets a fix to a helper reach nodes that already have an
 * older build. That matters most for the redactor: a node running stale redaction logic still
 * reports {@code REDACTION_STATUS=NODE}, which the collector trusts without re-checking, so a
 * silently outdated binary would produce a bundle that looks safely redacted and is not.
 */
@Slf4j
@Singleton
public class SupportBundleNodeBinarySupport {

  static final long INSTALL_TIMEOUT_SECS = 60L;

  private final NodeUniverseManager nodeUniverseManager;

  /**
   * One deployment decision per bundle, node and binary. Without it every component repeats the
   * same {@code uname -m} and {@code md5sum} on the same node -- roughly 34 round-trips per node
   * for the log-parse helper alone, where two suffice. Failures are cached too: each helper's
   * fallback is safe, and re-attempting a failing upload once per component helps nobody.
   *
   * <p>The deploy runs under its entry's lock, not inside {@code computeIfAbsent}: its remote calls
   * would hold the map's bin lock and stall other nodes and bundles whose keys share the bin.
   */
  private final ConcurrentHashMap<String, DeployEntry> deployedByBundleNodeBinary =
      new ConcurrentHashMap<>();

  @Inject
  public SupportBundleNodeBinarySupport(NodeUniverseManager nodeUniverseManager) {
    this.nodeUniverseManager = nodeUniverseManager;
  }

  /**
   * Drops this bundle's cached deployment decisions. In-memory only -- the binaries stay on the
   * node between bundles, which is what the checksum comparison above exists to exploit. The next
   * bundle re-checks rather than inheriting this one's answer.
   */
  public void forgetBundle(UUID bundleUuid) {
    if (bundleUuid == null) {
      return;
    }
    String prefix = bundleUuid + "|";
    deployedByBundleNodeBinary.keySet().removeIf(key -> key.startsWith(prefix));
  }

  /**
   * What to do when the YBA host has no local copy to compare the node's against. That happens on a
   * YBA whose {@code devops/bin} was never populated, and more often whenever {@link
   * #detectNodeGoArch} fails -- an unreadable {@code uname -m} or an unmapped architecture both
   * leave nothing to verify against.
   */
  enum UnverifiedNodeCopy {
    /**
     * Use whatever the node already has. Right where a stale build costs accuracy, not safety: log
     * parsing degrades to Python, and {@code collect_cdc_state} fails the component outright, so
     * refusing would lose the data rather than protect it.
     */
    TRUST,

    /**
     * Refuse it. Required for the redactor: a node running older redaction logic still reports
     * {@code REDACTION_STATUS=NODE}, which the collector trusts without re-checking, so the bundle
     * would look redacted and not be. Refusing makes the caller send {@code --drop_server_conf}, so
     * the node drops its server.conf rather than ship it under-redacted.
     */
    REJECT
  }

  /**
   * The context to install and check helpers under, given the one a component's script runs under.
   * Helpers live under {@code <ybHomeDir>/bin}, owned by the spec's user (yugabyte if none), which
   * a provider-user run cannot write to.
   *
   * <p>Every bundle installs and checks as this user, so a later bundle finds an earlier one's copy
   * by checksum instead of uploading it again. Estimates install nothing; they stage their script
   * under this context too.
   *
   * <p>Capped at {@link #INSTALL_TIMEOUT_SECS} rather than the script's timeout, which can be an
   * hour: a helper is optional, so a hung install mustn't use up the component's time.
   */
  public static ShellProcessContext installContext(
      ShellProcessContext runContext, String linuxUser) {
    long runTimeoutSecs = runContext.getTimeoutSecs();
    return runContext.toBuilder()
        .sshUser(StringUtils.defaultIfBlank(linuxUser, NodeManager.YUGABYTE_USER))
        .timeoutSecs(
            runTimeoutSecs > 0
                ? Math.min(runTimeoutSecs, INSTALL_TIMEOUT_SECS)
                : INSTALL_TIMEOUT_SECS)
        .build();
  }

  String remoteBinaryPath(String binaryName, NodeDetails node, Universe universe) {
    return nodeUniverseManager.getYbHomeDir(node, universe) + "/bin/" + binaryName;
  }

  static String archSpecificBinaryName(String binaryName, String goArch) {
    return binaryName + "-linux-" + goArch;
  }

  /**
   * Reads the node's architecture rather than assuming the YBA host's, since a YBA on x86 routinely
   * manages arm64 nodes.
   */
  Optional<String> detectNodeGoArch(
      NodeDetails node, Universe universe, ShellProcessContext context) {
    try {
      ShellResponse response =
          nodeUniverseManager.runCommand(node, universe, Arrays.asList("uname", "-m"), context);
      SupportBundleCancellation.throwIfCancelled(response);
      if (!response.isSuccess()) {
        log.warn(
            "Failed to detect architecture on node {} (exit code {}); cannot deploy helper",
            node.getNodeName(),
            response.getCode());
        return Optional.empty();
      }
      String goArch = mapUnameToGoArch(response.extractRunCommandOutput());
      if (goArch == null) {
        log.warn(
            "Unsupported node architecture '{}' on node {}; cannot deploy helper",
            response.extractRunCommandOutput(),
            node.getNodeName());
      }
      return Optional.ofNullable(goArch);
    } catch (Exception e) {
      SupportBundleCancellation.rethrowIfCancelled(e);
      log.warn(
          "Failed to detect architecture on node {}; cannot deploy helper", node.getNodeName(), e);
      return Optional.empty();
    }
  }

  static String mapUnameToGoArch(String unameArch) {
    if (unameArch == null) {
      return null;
    }
    switch (unameArch.trim()) {
      case "x86_64":
        return "amd64";
      case "aarch64":
      case "arm64":
        return "arm64";
      default:
        return null;
    }
  }

  /**
   * Only the arch-suffixed name is accepted. Nothing produces a bare-named binary in {@code
   * devops/bin}, and falling back to one would risk shipping an x86 binary to an arm node, where it
   * fails with an exec format error the scripts cannot tell apart from a real failure.
   */
  Optional<Path> resolveLocalBinaryPath(String binaryName, Optional<String> goArch) {
    Path binDir = Paths.get(AppConfigHelper.getDevopsHomePath(), "bin");
    if (goArch.isPresent()) {
      Path archSpecific = binDir.resolve(archSpecificBinaryName(binaryName, goArch.get()));
      if (Files.isRegularFile(archSpecific)) {
        return Optional.of(archSpecific);
      }
      log.warn(
          "Arch-specific {} binary missing at {}; cannot deploy helper", binaryName, archSpecific);
    }
    return Optional.empty();
  }

  /**
   * Ensures the node holds the same build of {@code binaryName} that YBA has, uploading it when it
   * is absent or differs. Returns false when no usable binary could be put in place; {@code
   * consequenceOnFailure} describes to the operator what that costs for this helper.
   */
  boolean ensureBinaryOnNode(
      UUID bundleUuid,
      String binaryName,
      NodeDetails node,
      Universe universe,
      ShellProcessContext context,
      String consequenceOnFailure,
      UnverifiedNodeCopy unverifiedNodeCopy) {
    if (bundleUuid == null) {
      // No bundle to scope the decision to: do the work every time.
      return deployBinaryOnNode(
          binaryName, node, universe, context, consequenceOnFailure, unverifiedNodeCopy);
    }
    String key = bundleUuid + "|" + node.getNodeName() + "|" + binaryName;
    DeployEntry entry =
        deployedByBundleNodeBinary.computeIfAbsent(key, ignored -> new DeployEntry());
    synchronized (entry) {
      // A cancelled deploy throws, leaving it unset for the next caller.
      if (entry.deployed == null) {
        entry.deployed =
            deployBinaryOnNode(
                binaryName, node, universe, context, consequenceOnFailure, unverifiedNodeCopy);
      }
      return entry.deployed;
    }
  }

  private static final class DeployEntry {
    // Read and written only under the entry's lock.
    Boolean deployed;
  }

  private boolean deployBinaryOnNode(
      String binaryName,
      NodeDetails node,
      Universe universe,
      ShellProcessContext context,
      String consequenceOnFailure,
      UnverifiedNodeCopy unverifiedNodeCopy) {
    String remotePath = remoteBinaryPath(binaryName, node, universe);
    Optional<Path> localBinary =
        resolveLocalBinaryPath(binaryName, detectNodeGoArch(node, universe, context));
    if (localBinary.isEmpty()) {
      if (unverifiedNodeCopy == UnverifiedNodeCopy.REJECT) {
        log.warn(
            "{} not available on the YBA host under {}; refusing to use the unverified copy on"
                + " node {}; {}",
            binaryName,
            Paths.get(AppConfigHelper.getDevopsHomePath(), "bin"),
            node.getNodeName(),
            consequenceOnFailure);
        return false;
      }
      if (remoteBinaryExists(binaryName, node, universe, remotePath, context)) {
        log.warn(
            "{} not available on the YBA host under {}; using the copy already on node {} without"
                + " verifying it is current",
            binaryName,
            Paths.get(AppConfigHelper.getDevopsHomePath(), "bin"),
            node.getNodeName());
        return true;
      }
      log.warn(
          "{} helper missing under {} on YBA host and absent on node {}; {}",
          binaryName,
          Paths.get(AppConfigHelper.getDevopsHomePath(), "bin"),
          node.getNodeName(),
          consequenceOnFailure);
      return false;
    }

    try {
      if (!nodeBinaryNeedsSync(
          binaryName, node, universe, remotePath, localBinary.get(), context)) {
        return true;
      }
      String binDir = nodeUniverseManager.getYbHomeDir(node, universe) + "/bin";
      nodeUniverseManager
          .runCommand(node, universe, Arrays.asList("mkdir", "-p", binDir), context)
          .processErrors();
      uploadThenRename(node, universe, localBinary.get(), remotePath, context);
      // Verified after upload because uploadFileToNode can return without error while the file is
      // not usable on the far side, for instance on a full or noexec filesystem.
      if (SupportBundleNodeStaging.fileExists(
          nodeUniverseManager, node, universe, remotePath, context)) {
        log.info(
            "Deployed {} from {} to {} on node {}",
            binaryName,
            localBinary.get().getFileName(),
            remotePath,
            node.getNodeName());
        return true;
      }
      log.warn(
          "Uploaded {} to {} on node {} but verification failed; {}",
          binaryName,
          remotePath,
          node.getNodeName(),
          consequenceOnFailure);
      return false;
    } catch (Exception e) {
      // Thrown, not returned, so a cancelled deploy isn't cached for the bundle.
      SupportBundleCancellation.rethrowIfCancelled(e);
      log.warn(
          "Failed to deploy {} on node {}; {}",
          binaryName,
          node.getNodeName(),
          consequenceOnFailure,
          e);
      return false;
    }
  }

  /**
   * Writing the live path in place fails with "Text file busy" while another bundle runs the
   * binary, and a failed upload would leave it truncated. A rename in the same directory is atomic:
   * a running copy keeps its file, and a new exec only sees a complete one.
   */
  private void uploadThenRename(
      NodeDetails node,
      Universe universe,
      Path localBinary,
      String remotePath,
      ShellProcessContext context) {
    String tmpPath = remotePath + ".tmp." + UUID.randomUUID();
    boolean renamed = false;
    try {
      nodeUniverseManager.uploadFileToNode(
          node, universe, localBinary.toString(), tmpPath, "755", context);
      nodeUniverseManager
          .runCommand(
              node, universe, Arrays.asList("mv", "-f", tmpPath, remotePath), context, false)
          .processErrors();
      renamed = true;
    } finally {
      if (!renamed) {
        // Outside the bundle's directory, so nothing else removes it.
        try {
          ShellResponse removed =
              nodeUniverseManager.runCommand(
                  node, universe, Arrays.asList("rm", "-f", tmpPath), context, false);
          if (removed != null && !removed.isSuccess()) {
            log.warn(
                "Could not remove '{}' on node {}: {}",
                tmpPath,
                node.getNodeName(),
                removed.getMessage());
          }
        } catch (Exception e) {
          log.warn(
              "Could not remove '{}' on node {}: {}", tmpPath, node.getNodeName(), e.getMessage());
        }
      }
    }
  }

  /**
   * Compares checksums rather than testing for existence, so a node carrying an older build is
   * refreshed. One remote call, and anything inconclusive -- the file missing or unreadable, the
   * node unreachable -- is treated as needing a sync, since re-uploading is cheap and running a
   * stale helper is not.
   */
  private boolean nodeBinaryNeedsSync(
      String binaryName,
      NodeDetails node,
      Universe universe,
      String remotePath,
      Path localBinary,
      ShellProcessContext context)
      throws Exception {
    Optional<String> remoteMd5 =
        SupportBundleRemoteChecksum.remoteMd5(
            nodeUniverseManager, node, universe, remotePath, context);
    if (!remoteMd5.isPresent()) {
      return true;
    }
    String localMd5 = FileUtils.getFileChecksum(localBinary.toString());
    if (localMd5.equalsIgnoreCase(remoteMd5.get())) {
      return false;
    }
    log.info(
        "{} on node {} differs from the YBA copy; replacing it", binaryName, node.getNodeName());
    return true;
  }

  private boolean remoteBinaryExists(
      String binaryName,
      NodeDetails node,
      Universe universe,
      String remotePath,
      ShellProcessContext context) {
    try {
      // As the context's user: the default user can report a present binary as missing when the
      // yb home is not traversable by it.
      return SupportBundleNodeStaging.fileExists(
          nodeUniverseManager, node, universe, remotePath, context);
    } catch (Exception e) {
      SupportBundleCancellation.rethrowIfCancelled(e);
      log.warn("Failed to check {} on node {}", binaryName, node.getNodeName(), e);
      return false;
    }
  }
}
