// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundlev2;

import com.google.common.cache.Cache;
import com.google.common.cache.CacheBuilder;
import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.ShellProcessContext;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.UUID;
import java.util.concurrent.CancellationException;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.TimeUnit;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

/**
 * Copies the scripts a bundle runs on a node into a per-bundle directory under the node's tmp
 * directory ({@link NodeUniverseManager#getRemoteTmpDir}), once per file, and removes the directory
 * when the bundle ends. Commands then run the copied file instead of streaming it: a streamed
 * script travels as one command-line argument, which Linux caps at 128 KiB. The scripts' work
 * directories and tars go in the same directory, so whatever a killed run leaves goes with it.
 */
@Slf4j
@Singleton
public class SupportBundleNodeStaging {

  static final String DIR_NAME_PREFIX = "yba-support-bundle-";
  static final String FALLBACK_TMP_DIR = "/tmp";
  private static final long CLEANUP_TIMEOUT_SECS = 60L;
  // Prints its answer: over SSH any non-zero exit reads as a failed command.
  static final String FILE_EXISTS_SCRIPT = "if [ -e \"$1\" ]; then echo 1; else echo 0; fi";

  private final NodeUniverseManager nodeUniverseManager;
  // Per staging id (a bundle or one estimate) and node: its directory there and the files copied
  // in, so each file uploads once and cleanup knows which nodes to clean and where.
  private final Map<Key, StagedNode> staged = new ConcurrentHashMap<>();
  // Ids whose bundle has ended. An abort stops waiting for the collection threads, and a late one
  // would otherwise recreate the removed directory. Expires so the ids don't pile up.
  private final Cache<UUID, Boolean> closed =
      CacheBuilder.newBuilder().expireAfterWrite(1, TimeUnit.DAYS).build();

  @Inject
  public SupportBundleNodeStaging(NodeUniverseManager nodeUniverseManager) {
    this.nodeUniverseManager = nodeUniverseManager;
  }

  /**
   * Returns the copy of {@code localFile} on the node, uploading it the first time this staging id
   * needs it there. Throws when it can't be staged.
   */
  public String stage(
      UUID stagingId,
      Universe universe,
      NodeDetails node,
      Path localFile,
      ShellProcessContext context) {
    Objects.requireNonNull(stagingId, "stagingId");
    StagedNode stagedNode = stagedNode(stagingId, universe, node, context);
    return stagedNode.copies.computeIfAbsent(
        localFile.getFileName().toString(),
        name -> upload(stagedNode.dir, universe, node, localFile, context));
  }

  /**
   * This id's directory on the node, created the first time. Under the node's tmp directory, or
   * {@code /tmp} when that can't be resolved or created.
   */
  public String dir(
      UUID stagingId, Universe universe, NodeDetails node, ShellProcessContext context) {
    Objects.requireNonNull(stagingId, "stagingId");
    return stagedNode(stagingId, universe, node, context).dir;
  }

  private StagedNode stagedNode(
      UUID stagingId, Universe universe, NodeDetails node, ShellProcessContext context) {
    throwIfClosed(stagingId);
    Key key = Key.of(stagingId, node);
    StagedNode stagedNode = staged.computeIfAbsent(key, ignored -> new StagedNode(node));
    synchronized (stagedNode) {
      if (stagedNode.dir == null) {
        // Again under the lock cleanup takes: it may have closed the id and passed this entry.
        if (closed.getIfPresent(stagingId) != null) {
          staged.remove(key, stagedNode);
          throwIfClosed(stagingId);
        }
        stagedNode.dir = createDir(stagingId, universe, node, context);
      }
    }
    return stagedNode;
  }

  private void throwIfClosed(UUID stagingId) {
    if (closed.getIfPresent(stagingId) != null) {
      // Cancellation, not a failure: only a thread that outlived its bundle's abort gets here.
      throw new CancellationException("Support bundle staging " + stagingId + " is closed");
    }
  }

  /** Refuses any later {@link #stage} under this id; call it before {@link #cleanup}. */
  public void close(UUID stagingId) {
    if (stagingId != null) {
      closed.put(stagingId, Boolean.TRUE);
    }
  }

  /** Whether {@code fileName} was staged on the node under this id. */
  public boolean isStaged(UUID stagingId, NodeDetails node, String fileName) {
    StagedNode stagedNode = staged.get(Key.of(stagingId, node));
    return stagedNode != null && stagedNode.copies.containsKey(fileName);
  }

  /**
   * Removes this id's directory from every node it was staged on and forgets them. Best effort and
   * bounded, since callers run it in a {@code finally}. Returns those nodes.
   */
  public List<NodeDetails> cleanup(UUID stagingId, Universe universe) {
    List<NodeDetails> nodes = new ArrayList<>();
    if (stagingId == null) {
      return nodes;
    }
    ShellProcessContext context =
        ShellProcessContext.builder().logCmdOutput(true).timeoutSecs(CLEANUP_TIMEOUT_SECS).build();
    staged
        .entrySet()
        .removeIf(
            entry -> {
              if (!stagingId.equals(entry.getKey().stagingId)) {
                return false;
              }
              StagedNode stagedNode = entry.getValue();
              nodes.add(stagedNode.node);
              // Waits for a directory being created, so it is removed rather than left behind.
              synchronized (stagedNode) {
                removeDir(stagedNode.dir, universe, stagedNode.node, context);
              }
              return true;
            });
    return nodes;
  }

  /**
   * Whether {@code path} exists on the node, checked as the context's user. Used instead of
   * NodeUniverseManager.checkNodeIfFileExists, which streams all of node_utils.sh for each check.
   * Throws when the check can't run.
   */
  public static boolean fileExists(
      NodeUniverseManager nodeUniverseManager,
      NodeDetails node,
      Universe universe,
      String path,
      ShellProcessContext context) {
    ShellResponse response =
        nodeUniverseManager.runCommand(node, universe, fileExistsCommand(path), context, false);
    SupportBundleCancellation.throwIfCancelled(response);
    if (!response.isSuccess()) {
      throw new RuntimeException(
          String.format(
              "Could not check %s on node '%s': %s",
              path, node.getNodeName(), response.getMessage()));
    }
    return response.extractRunCommandOutput().equals("1");
  }

  /** The path is bash's {@code $1}, so it is never parsed as shell. */
  public static List<String> fileExistsCommand(String path) {
    return List.of("bash", "-c", FILE_EXISTS_SCRIPT, "bash", path);
  }

  static String remoteDir(String tmpDir, UUID stagingId, NodeDetails node) {
    return tmpDir + "/" + DIR_NAME_PREFIX + stagingId + "-" + nodeKey(node);
  }

  /**
   * The private IP rather than the node name: it is unique within a universe and is what the node
   * is addressed by. Dots and colons are replaced so an IPv6 address stays one path segment.
   */
  static String nodeKey(NodeDetails node) {
    String key =
        node.cloudInfo != null && node.cloudInfo.private_ip != null
            ? node.cloudInfo.private_ip
            : node.getNodeName();
    return key == null ? "unknown" : key.replace('.', '_').replace(':', '_');
  }

  private String createDir(
      UUID stagingId, Universe universe, NodeDetails node, ShellProcessContext context) {
    String tmpDir = FALLBACK_TMP_DIR;
    try {
      tmpDir =
          StringUtils.defaultIfBlank(nodeUniverseManager.getRemoteTmpDir(node, universe), tmpDir);
    } catch (Exception e) {
      log.warn(
          "Could not resolve the tmp directory of node '{}', staging under {}: {}",
          node.getNodeName(),
          FALLBACK_TMP_DIR,
          e.getMessage());
    }
    String dir = remoteDir(tmpDir, stagingId, node);
    ShellResponse mkdir = mkdir(universe, node, dir, context);
    if (!mkdir.isSuccess() && !FALLBACK_TMP_DIR.equals(tmpDir)) {
      String fallback = remoteDir(FALLBACK_TMP_DIR, stagingId, node);
      log.warn(
          "Could not create {} on node '{}', staging under {}: {}",
          dir,
          node.getNodeName(),
          FALLBACK_TMP_DIR,
          mkdir.getMessage());
      dir = fallback;
      mkdir = mkdir(universe, node, dir, context);
    }
    if (!mkdir.isSuccess()) {
      throw new RuntimeException(
          String.format(
              "Could not create %s on node '%s': %s", dir, node.getNodeName(), mkdir.getMessage()));
    }
    return dir;
  }

  // An explicit mode, not the yugabyte user's umask: SystemLogs runs these files as the provider
  // SSH user, who must be able to read them.
  private ShellResponse mkdir(
      Universe universe, NodeDetails node, String dir, ShellProcessContext context) {
    ShellResponse mkdir =
        nodeUniverseManager.runCommand(
            node, universe, List.of("mkdir", "-p", "-m", "755", dir), context, false);
    return SupportBundleCancellation.throwIfCancelled(mkdir);
  }

  private String upload(
      String dir,
      Universe universe,
      NodeDetails node,
      Path localFile,
      ShellProcessContext context) {
    String remotePath = dir + "/" + localFile.getFileName();
    nodeUniverseManager.uploadFileToNode(
        node, universe, localFile.toString(), remotePath, "755", context);
    return remotePath;
  }

  private void removeDir(
      String dir, Universe universe, NodeDetails node, ShellProcessContext context) {
    if (dir == null) {
      return;
    }
    try {
      ShellResponse response =
          nodeUniverseManager.runCommand(node, universe, List.of("rm", "-rf", dir), context, false);
      if (response.isSuccess()) {
        log.info("Removed support bundle directory {} on node '{}'", dir, node.getNodeName());
      } else {
        log.warn(
            "Could not remove support bundle staging on node '{}': {}",
            node.getNodeName(),
            response.getMessage());
      }
    } catch (Exception e) {
      log.warn(
          "Could not remove support bundle staging on node '{}': {}",
          node.getNodeName(),
          e.getMessage());
    }
  }

  /**
   * What one staging id has put on one node: its directory and the scripts copied there. Its lock
   * orders creating the directory against cleanup.
   */
  private static final class StagedNode {
    final NodeDetails node;
    // File name to the path of its copy here: the scripts YBA runs on the node, nothing collected.
    final Map<String, String> copies = new ConcurrentHashMap<>();
    // Set once, under the node's lock.
    volatile String dir;

    StagedNode(NodeDetails node) {
      this.node = node;
    }
  }

  private record Key(UUID stagingId, String nodeKey) {
    static Key of(UUID stagingId, NodeDetails node) {
      return new Key(stagingId, SupportBundleNodeStaging.nodeKey(node));
    }
  }
}
