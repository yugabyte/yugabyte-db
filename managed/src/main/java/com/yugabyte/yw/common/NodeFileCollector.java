// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.NodeScriptRunner.NodeFilter;
import com.yugabyte.yw.models.FileCollection;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.Builder;
import lombok.Data;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

/**
 * Service for collecting files from multiple nodes in a universe synchronously and returning the
 * results. Reuses existing support bundle and node-agent infrastructure for file downloads.
 */
@Slf4j
@Singleton
public class NodeFileCollector {

  /** Maximum threads for parallel file collection to prevent OOM/thrashing */
  private static final int MAX_PARALLEL_THREADS = 50;

  /**
   * Subdirectory name (under the node's tmp directory) used to stage collected tar archives. The
   * staging dir is owned by the universe's default Linux user (yugabyte) and created without the
   * sticky bit, so the default-user-driven download and cleanup paths can read and delete tar files
   * even when the tar was written by an impersonated linux_user. Putting the tar directly in the
   * tmp dir would leak it forever in the impersonated case because the tmp dir's sticky bit blocks
   * non-owner deletion.
   */
  private static final String COLLECTION_TAR_SUBDIR = "yba-file-collections";

  private final NodeUniverseManager nodeUniverseManager;
  private final PlatformExecutorFactory executorFactory;

  @Inject
  public NodeFileCollector(
      NodeUniverseManager nodeUniverseManager, PlatformExecutorFactory executorFactory) {
    this.nodeUniverseManager = nodeUniverseManager;
    this.executorFactory = executorFactory;
  }

  /** Parameters for file collection */
  @Data
  @Builder
  public static class CollectionParams {
    private List<String> filePaths;
    private List<String> directoryPaths;
    @Builder.Default private int maxDepth = 1;
    @Builder.Default private long maxFileSizeBytes = 10 * 1024 * 1024; // 10MB
    @Builder.Default private long maxTotalSizeBytes = 100 * 1024 * 1024; // 100MB
    @Builder.Default private long timeoutSecs = 300;
    private String linuxUser;
  }

  /** Result from collecting a single file */
  @Data
  @Builder
  public static class FileResult {
    private String remotePath;
    private long fileSizeBytes;
    private boolean success;
    private String errorMessage;
    private boolean skipped;
    private String skipReason;
  }

  /** Result from collecting files from a single node */
  @Data
  @Builder
  public static class NodeResult {
    private String nodeName;
    private String nodeAddress;
    private boolean success;
    private int filesCollected;
    private int filesSkipped;
    private int filesFailed;
    private long totalBytesCollected;
    private long executionTimeMs;
    private String errorMessage;
    private List<FileResult> files;
    // Path to the tar.gz file created on the remote node (files are tarred but not downloaded)
    private String remoteTarPath;
  }

  /** Aggregated results from all nodes */
  @Data
  @Builder
  public static class CollectionResult {
    // Unique UUID for this collection - used to download files later
    private UUID collectionUuid;
    private int totalNodes;
    private int successfulNodes;
    private int failedNodes;
    private int totalFilesCollected;
    private int totalFilesSkipped;
    private int totalFilesFailed;
    private long totalBytesCollected;
    private long totalExecutionTimeMs;
    private boolean allSucceeded;
    private Map<String, NodeResult> nodeResults;
  }

  /**
   * Collect files from selected nodes in a universe and return results. Reuses the support bundle
   * infrastructure for file downloads via Node Agent or SSH. The collection metadata is stored in
   * database so files can be downloaded later using the collection ID.
   *
   * @param customerUuid The customer UUID
   * @param universe The universe to collect files from
   * @param collectionParams File collection configuration
   * @param nodeFilter Optional node selection criteria (null for all nodes)
   * @return Aggregated collection results including a collection ID for downloading
   */
  public CollectionResult collectFiles(
      UUID customerUuid,
      Universe universe,
      CollectionParams collectionParams,
      NodeFilter nodeFilter) {
    // Validate linux_user for manual on-prem providers
    Util.validateLinuxUserForOnPrem(collectionParams.getLinuxUser(), universe);

    List<NodeDetails> targetNodes = NodeScriptRunner.getTargetNodes(universe, nodeFilter);

    if (targetNodes.isEmpty()) {
      return CollectionResult.builder()
          .collectionUuid(null) // No collection created when no nodes
          .totalNodes(0)
          .successfulNodes(0)
          .failedNodes(0)
          .totalFilesCollected(0)
          .totalFilesSkipped(0)
          .totalFilesFailed(0)
          .totalBytesCollected(0)
          .allSucceeded(true)
          .nodeResults(new LinkedHashMap<>())
          .build();
    }

    long startTime = System.currentTimeMillis();

    // Run collection on all target nodes in parallel using shared executor utility
    int maxParallelNodes =
        (nodeFilter != null && nodeFilter.getMaxParallelNodes() > 0)
            ? nodeFilter.getMaxParallelNodes()
            : MAX_PARALLEL_THREADS;
    long waitTimeoutSecs = collectionParams.getTimeoutSecs() + 60;

    Map<String, ParallelNodeExecutor.TaskOutcome<NodeResult>> outcomes =
        ParallelNodeExecutor.execute(
            targetNodes,
            node -> collectFromNode(universe, node, collectionParams),
            maxParallelNodes,
            waitTimeoutSecs,
            executorFactory,
            "collect-files");

    // Convert task outcomes to NodeResults and aggregate metrics
    Map<String, NodeResult> results = new LinkedHashMap<>();
    int successCount = 0;
    int failCount = 0;
    int totalFilesCollected = 0;
    int totalFilesSkipped = 0;
    int totalFilesFailed = 0;
    long totalBytesCollected = 0;

    for (var entry : outcomes.entrySet()) {
      String nodeName = entry.getKey();
      ParallelNodeExecutor.TaskOutcome<NodeResult> outcome = entry.getValue();

      if (outcome.getStatus() == ParallelNodeExecutor.TaskStatus.SUCCESS) {
        NodeResult result = outcome.getResult();
        results.put(nodeName, result);
        if (result.isSuccess()) {
          successCount++;
        } else {
          failCount++;
        }
        totalFilesCollected += result.getFilesCollected();
        totalFilesSkipped += result.getFilesSkipped();
        totalFilesFailed += result.getFilesFailed();
        totalBytesCollected += result.getTotalBytesCollected();
      } else {
        // Build error result for non-success outcomes
        results.put(
            nodeName,
            NodeResult.builder()
                .nodeName(nodeName)
                .nodeAddress(outcome.getNode().cloudInfo.private_ip)
                .success(false)
                .filesCollected(0)
                .filesSkipped(0)
                .filesFailed(0)
                .totalBytesCollected(0)
                .executionTimeMs(outcome.getExecutionTimeMs())
                .errorMessage(outcome.getErrorMessage())
                .files(new ArrayList<>())
                .build());
        failCount++;
      }
    }

    long totalTime = System.currentTimeMillis() - startTime;

    // Store collection metadata in database for later download
    // Only store if we have successful tar files to download
    Map<String, String> nodeTarPaths = new LinkedHashMap<>();
    Map<String, String> nodeAddresses = new LinkedHashMap<>();
    for (Map.Entry<String, NodeResult> entry : results.entrySet()) {
      NodeResult result = entry.getValue();
      if (result.isSuccess() && result.getRemoteTarPath() != null) {
        nodeTarPaths.put(entry.getKey(), result.getRemoteTarPath());
        nodeAddresses.put(entry.getKey(), result.getNodeAddress());
      }
    }

    UUID collectionUuid = null;
    if (!nodeTarPaths.isEmpty()) {
      // Save to database instead of cache - survives restarts and works with HA
      FileCollection fileCollection =
          FileCollection.create(
              customerUuid, universe.getUniverseUUID(), nodeTarPaths, nodeAddresses);
      collectionUuid = fileCollection.getCollectionUuid();
    }

    return CollectionResult.builder()
        .collectionUuid(collectionUuid)
        .totalNodes(targetNodes.size())
        .successfulNodes(successCount)
        .failedNodes(failCount)
        .totalFilesCollected(totalFilesCollected)
        .totalFilesSkipped(totalFilesSkipped)
        .totalFilesFailed(totalFilesFailed)
        .totalBytesCollected(totalBytesCollected)
        .totalExecutionTimeMs(totalTime)
        .allSucceeded(failCount == 0)
        .nodeResults(results)
        .build();
  }

  private NodeResult collectFromNode(Universe universe, NodeDetails node, CollectionParams params) {
    long nodeStartTime = System.currentTimeMillis();
    List<FileResult> fileResults = new ArrayList<>();
    int collected = 0;
    int skipped = 0;
    int failed = 0;
    long bytesCollected = 0;
    ShellProcessContext context = buildShellContext(params);

    // Fail fast on unreachable nodes; otherwise per-file probes swallow the
    // connection error and the node is mis-reported as successful with file-level failures.
    if (!nodeUniverseManager.isNodeReachable(node, universe)) {
      long executionTime = System.currentTimeMillis() - nodeStartTime;
      return NodeResult.builder()
          .nodeName(node.nodeName)
          .nodeAddress(node.cloudInfo.private_ip)
          .success(false)
          .filesCollected(0)
          .filesSkipped(0)
          .filesFailed(0)
          .totalBytesCollected(0)
          .executionTimeMs(executionTime)
          .errorMessage("Node is unreachable")
          .files(new ArrayList<>())
          .build();
    }

    try {
      // Collect all file paths to tar
      List<String> filesToCollect = new ArrayList<>();
      Map<String, Long> fileSizeMap = new LinkedHashMap<>();

      // Add individual file paths - check existence and size before download
      if (params.getFilePaths() != null) {
        for (String filePath : params.getFilePaths()) {
          if (!nodeUniverseManager.checkNodeIfFileExists(node, universe, filePath, context)) {
            // checkNodeIfFileExists uses `test -e`, which returns false both when the file is
            // missing AND when the caller lacks traversal (+x) permission on a parent directory.
            // Surface both cases in the message instead of asserting "not found", which would be
            // misleading in the permission-denied case.
            fileResults.add(
                FileResult.builder()
                    .remotePath(filePath)
                    .success(false)
                    .errorMessage("File not found or cannot be accessed")
                    .build());
            failed++;
            continue;
          }

          // Verify the file is readable - tar will silently skip unreadable
          // entries (stderr is suppressed) so an empty/partial tar would
          // otherwise be reported as a successful collection.
          if (!isRemotePathReadable(node, universe, filePath, false, context)) {
            fileResults.add(
                FileResult.builder()
                    .remotePath(filePath)
                    .success(false)
                    .errorMessage("Permission denied: cannot read file " + filePath)
                    .build());
            failed++;
            continue;
          }

          // Get file size using stat command
          long fileSize = getRemoteFileSize(node, universe, filePath, context);

          // Check size limit before download
          if (fileSize > params.getMaxFileSizeBytes()) {
            fileResults.add(
                FileResult.builder()
                    .remotePath(filePath)
                    .fileSizeBytes(fileSize)
                    .success(false)
                    .skipped(true)
                    .skipReason(
                        String.format(
                            "File size (%d bytes) exceeds max_file_size_bytes limit (%d bytes)",
                            fileSize, params.getMaxFileSizeBytes()))
                    .build());
            skipped++;
            continue;
          }

          filesToCollect.add(filePath);
          fileSizeMap.put(filePath, fileSize);
        }
      }

      // If directory paths are specified, list files in those directories
      // Reuses NodeUniverseManager.getNodeFilePathAndSizes() from support bundle code
      if (params.getDirectoryPaths() != null) {
        for (String dirPath : params.getDirectoryPaths()) {
          try {
            // Resolve symlinks to get the real path (like support bundle does)
            String resolvedDirPath = resolveSymlink(node, universe, dirPath, context);

            // Verify the directory tree is readable+traversable up to maxDepth before listing.
            // get_paths_and_sizes pipes find into awk, so find's stderr is discarded and a
            // permission-denied path (at the root OR within any subdirectory we'd descend into)
            // silently returns an empty listing - which would otherwise look like a successful
            // collection of zero files. The check is delegated to node_utils.sh so it can use
            // `stat` to tell "missing" apart from "permission denied", and walk the tree to the
            // same depth that the listing will.
            DirAccessStatus accessStatus =
                checkDirectoryAccessible(
                    node, universe, resolvedDirPath, params.getMaxDepth(), context);
            if (accessStatus != DirAccessStatus.OK) {
              String message;
              switch (accessStatus) {
                case MISSING:
                  message = "Directory not found: " + dirPath;
                  break;
                case DENIED:
                  message =
                      "Permission denied: cannot read directory "
                          + dirPath
                          + " or a subdirectory within maxDepth="
                          + params.getMaxDepth();
                  break;
                default:
                  message = "Failed to verify access to directory " + dirPath;
                  break;
              }
              fileResults.add(
                  FileResult.builder()
                      .remotePath(dirPath)
                      .success(false)
                      .errorMessage(message)
                      .build());
              failed++;
              continue;
            }

            Map<String, Long> filesInDir =
                nodeUniverseManager.getNodeFilePathAndSizes(
                    node, universe, resolvedDirPath, params.getMaxDepth(), "f", context);
            for (Map.Entry<String, Long> entry : filesInDir.entrySet()) {
              String filePath = entry.getKey();
              Long fileSize = entry.getValue();
              if (fileSize > params.getMaxFileSizeBytes()) {
                fileResults.add(
                    FileResult.builder()
                        .remotePath(filePath)
                        .fileSizeBytes(fileSize)
                        .success(false)
                        .skipped(true)
                        .skipReason(
                            String.format(
                                "File size (%d bytes) exceeds max_file_size_bytes limit (%d bytes)",
                                fileSize, params.getMaxFileSizeBytes()))
                        .build());
                skipped++;
                continue;
              }
              filesToCollect.add(filePath);
              fileSizeMap.put(filePath, fileSize);
            }
          } catch (Exception e) {
            log.warn("Failed to list files in directory {} on node {}", dirPath, node.nodeName, e);
            fileResults.add(
                FileResult.builder()
                    .remotePath(dirPath)
                    .success(false)
                    .errorMessage("Failed to list directory: " + e.getMessage())
                    .build());
            failed++;
          }
        }
      }

      if (filesToCollect.isEmpty()) {
        long executionTime = System.currentTimeMillis() - nodeStartTime;
        return NodeResult.builder()
            .nodeName(node.nodeName)
            .nodeAddress(node.cloudInfo.private_ip)
            .success(failed == 0)
            .filesCollected(0)
            .filesSkipped(skipped)
            .filesFailed(failed)
            .totalBytesCollected(0)
            .executionTimeMs(executionTime)
            .files(fileResults)
            .build();
      }

      // Check total size limit
      long totalSize = fileSizeMap.values().stream().mapToLong(Long::longValue).sum();
      if (totalSize > params.getMaxTotalSizeBytes()) {
        // Need to skip some files to stay under limit
        long runningTotal = 0;
        List<String> filteredFiles = new ArrayList<>();
        for (String filePath : filesToCollect) {
          Long size = fileSizeMap.getOrDefault(filePath, 0L);
          if (runningTotal + size <= params.getMaxTotalSizeBytes()) {
            filteredFiles.add(filePath);
            runningTotal += size;
          } else {
            fileResults.add(
                FileResult.builder()
                    .remotePath(filePath)
                    .fileSizeBytes(size)
                    .success(false)
                    .skipped(true)
                    .skipReason("Total size limit reached")
                    .build());
            skipped++;
          }
        }
        filesToCollect = filteredFiles;
      }

      String remoteTarPath = null;

      if (!filesToCollect.isEmpty()) {
        // Create tar.gz on the remote node (but don't download to YBA)
        // This allows a separate API to retrieve the tar file later

        // Convert absolute paths to relative paths from root
        List<String> relativeFiles =
            filesToCollect.stream()
                .map(f -> f.startsWith("/") ? f.substring(1) : f)
                .collect(Collectors.toList());

        // Create tar on remote node using node_utils.sh create_tar_file
        String tarFileName =
            String.format("collected-files-%s-%s.tar.gz", node.nodeName, UUID.randomUUID());
        remoteTarPath =
            createTarOnRemoteNode(node, universe, "/", relativeFiles, tarFileName, context);

        if (remoteTarPath != null) {
          // Record successful file collections
          for (String filePath : filesToCollect) {
            long fileSize = fileSizeMap.getOrDefault(filePath, 0L);
            fileResults.add(
                FileResult.builder()
                    .remotePath(filePath)
                    .fileSizeBytes(fileSize)
                    .success(true)
                    .build());
            collected++;
            bytesCollected += fileSize;
          }
        } else {
          // Tar creation failed
          for (String filePath : filesToCollect) {
            fileResults.add(
                FileResult.builder()
                    .remotePath(filePath)
                    .success(false)
                    .errorMessage("Failed to create tar on remote node")
                    .build());
            failed++;
          }
        }
      }

      long executionTime = System.currentTimeMillis() - nodeStartTime;

      return NodeResult.builder()
          .nodeName(node.nodeName)
          .nodeAddress(node.cloudInfo.private_ip)
          .success(failed == 0)
          .filesCollected(collected)
          .filesSkipped(skipped)
          .filesFailed(failed)
          .totalBytesCollected(bytesCollected)
          .executionTimeMs(executionTime)
          .files(fileResults)
          .remoteTarPath(remoteTarPath)
          .build();

    } catch (Exception e) {
      long executionTime = System.currentTimeMillis() - nodeStartTime;
      log.error("Error collecting files from node {}: {}", node.nodeName, e.getMessage(), e);

      return NodeResult.builder()
          .nodeName(node.nodeName)
          .nodeAddress(node.cloudInfo.private_ip)
          .success(false)
          .filesCollected(collected)
          .filesSkipped(skipped)
          .filesFailed(failed)
          .totalBytesCollected(bytesCollected)
          .executionTimeMs(executionTime)
          .errorMessage(e.getMessage())
          .files(fileResults)
          .build();
    }
  }

  /**
   * Get the size of a single file on a remote node using stat command.
   *
   * @param node The target node
   * @param universe The universe
   * @param filePath The absolute path to the file
   * @return File size in bytes, or 0 if unable to determine
   */
  private long getRemoteFileSize(
      NodeDetails node, Universe universe, String filePath, ShellProcessContext context) {
    try {
      // Use stat to get file size - format %s gives size in bytes
      // Using -L to follow symlinks
      List<String> cmd = List.of("stat", "-L", "-c", "%s", filePath);
      ShellResponse response = nodeUniverseManager.runCommand(node, universe, cmd, context);

      if (response.getCode() == 0 && response.getMessage() != null) {
        String output = response.extractRunCommandOutput();
        return Long.parseLong(output);
      }
    } catch (Exception e) {
      log.warn(
          "Failed to get file size for {} on node {}: {}", filePath, node.nodeName, e.getMessage());
    }
    return 0L;
  }

  /** Result of {@link #checkDirectoryAccessible}, mirroring node_utils.sh's status strings. */
  private enum DirAccessStatus {
    OK,
    MISSING,
    DENIED,
    CHECK_FAILED
  }

  /**
   * Verifies a remote directory and every descendant up to maxDepth is readable+traversable by the
   * running user. Delegates to node_utils.sh's check_dir_accessible so we use `stat` (not `test
   * -e`) for missing-vs-denied disambiguation and walk the tree to the same depth that the
   * subsequent listing will. Returns CHECK_FAILED on script errors or unexpected output so callers
   * can surface a generic failure rather than a misleading OK.
   */
  private DirAccessStatus checkDirectoryAccessible(
      NodeDetails node,
      Universe universe,
      String dirPath,
      int maxDepth,
      ShellProcessContext context) {
    try {
      List<String> params = List.of("check_dir_accessible", dirPath, String.valueOf(maxDepth));
      ShellResponse response =
          nodeUniverseManager.runScript(
              node, universe, NodeUniverseManager.NODE_UTILS_SCRIPT, params, context);
      if (!response.isSuccess()) {
        log.warn(
            "check_dir_accessible failed for {} on node {}: {}",
            dirPath,
            node.nodeName,
            response.getMessage());
        return DirAccessStatus.CHECK_FAILED;
      }
      // The shell function may emit multiple lines (e.g. find's diagnostics); only the first
      // line carries the status code we contract on.
      String[] lines = response.extractRunCommandOutput().trim().split("\\R", 2);
      String status = lines.length == 0 ? "" : lines[0].trim();
      switch (status) {
        case "OK":
          return DirAccessStatus.OK;
        case "MISSING":
          return DirAccessStatus.MISSING;
        case "DENIED":
          return DirAccessStatus.DENIED;
        default:
          log.warn(
              "Unexpected check_dir_accessible output for {} on node {}: '{}'",
              dirPath,
              node.nodeName,
              status);
          return DirAccessStatus.CHECK_FAILED;
      }
    } catch (Exception e) {
      log.warn(
          "Failed to check directory access for {} on node {}: {}",
          dirPath,
          node.nodeName,
          e.getMessage());
      return DirAccessStatus.CHECK_FAILED;
    }
  }

  /**
   * Check whether a remote path is readable by the running user. For directories, both read and
   * execute (traverse) permissions are required to list the contents.
   *
   * @param node The target node
   * @param universe The universe
   * @param path The absolute remote path
   * @param isDirectory Whether the path is expected to be a directory
   * @return true if the path is accessible for reading, false otherwise (including any error)
   */
  private boolean isRemotePathReadable(
      NodeDetails node,
      Universe universe,
      String path,
      boolean isDirectory,
      ShellProcessContext context) {
    try {
      String quoted = shellSingleQuote(path);
      String testExpr =
          isDirectory
              ? String.format("test -r %s && test -x %s", quoted, quoted)
              : String.format("test -r %s", quoted);
      List<String> cmd = List.of("bash", "-c", testExpr);
      ShellResponse response = nodeUniverseManager.runCommand(node, universe, cmd, context);
      return response.getCode() == 0;
    } catch (Exception e) {
      log.warn(
          "Failed to check read access for {} on node {}: {}", path, node.nodeName, e.getMessage());
      return false;
    }
  }

  private static String shellSingleQuote(String s) {
    return "'" + s.replace("'", "'\\''") + "'";
  }

  /**
   * Build the ShellProcessContext used for every remote command in this collection. Honors the
   * caller-supplied linux_user; falls back to the platform default (yugabyte) when blank, matching
   * the behavior of NodeScriptRunner.
   */
  private ShellProcessContext buildShellContext(CollectionParams params) {
    if (StringUtils.isBlank(params.getLinuxUser())) {
      return ShellProcessContext.DEFAULT;
    }
    return ShellProcessContext.builder().sshUser(params.getLinuxUser()).build();
  }

  /**
   * Resolve symlinks on a remote node path using readlink -f.
   *
   * @param node The target node
   * @param universe The universe
   * @param path The path that might be a symlink
   * @return The resolved real path, or original path if resolution fails
   */
  private String resolveSymlink(
      NodeDetails node, Universe universe, String path, ShellProcessContext context) {
    try {
      List<String> cmd = List.of("readlink", "-f", path);
      ShellResponse response = nodeUniverseManager.runCommand(node, universe, cmd, context);

      if (response.getCode() == 0 && response.getMessage() != null) {
        String resolvedPath = response.extractRunCommandOutput();
        if (!resolvedPath.isEmpty() && !resolvedPath.equals(path)) {
          log.debug("Resolved symlink {} -> {} on node {}", path, resolvedPath, node.nodeName);
          return resolvedPath;
        }
      }
    } catch (Exception e) {
      log.warn("Failed to resolve symlink {} on node {}: {}", path, node.nodeName, e.getMessage());
    }
    return path;
  }

  /**
   * Create a tar.gz archive on the remote node containing the specified files.
   *
   * @param node The target node
   * @param universe The universe
   * @param baseDir The base directory for relative paths (usually "/")
   * @param relativeFiles List of file paths relative to baseDir
   * @param tarFileName Name of the tar file to create
   * @return The full path to the tar file on the remote node, or null if creation failed
   */
  private String createTarOnRemoteNode(
      NodeDetails node,
      Universe universe,
      String baseDir,
      List<String> relativeFiles,
      String tarFileName,
      ShellProcessContext context) {
    try {
      // Defensive: node.nodeName is interpolated into a single-quoted sed expression below
      // (--transform='s,^,<nodeName>/,'). Today YBA only produces names like "yb-<universe>-nN",
      // but reject anything outside the shell-safe set so a future schema change can't silently
      // turn into command injection or a broken tar.
      if (!Util.isShellSafeIdentifier(node.nodeName)) {
        throw new IllegalStateException("Unexpected node name for tar transform: " + node.nodeName);
      }

      // Resolve the per-node tmp directory and use it as the parent of the tar staging dir and
      // for the short-lived file list. Going through nodeUniverseManager.getRemoteTmpDir (rather
      // than calling GFlagsUtil.getCustomTmpDirectory directly) is intentional: it is the
      // established pattern in this file and wraps the gflag lookup with a "/tmp" fallback for
      // null/empty values. This honors the TMP_DIRECTORY gflag so we don't hardcode /tmp, which
      // may be unwritable for non-root users on hardened images.
      String remoteTmpDir = nodeUniverseManager.getRemoteTmpDir(node, universe);
      String collectionTarDir = remoteTmpDir + "/" + COLLECTION_TAR_SUBDIR;

      // Pre-create the shared tar staging directory as the default user so it is yugabyte-owned
      // with no sticky bit. linux_user can write the tar inside, and yugabyte retains the right
      // to delete the tar later via its ownership of the parent dir, regardless of who the tar
      // file itself ends up owned by.
      ensureCollectionTarDir(node, universe, collectionTarDir);

      // Create file list content
      String fileListContent = String.join("\n", relativeFiles);

      // Create a temporary file locally with the file list
      Path localTempFile = Files.createTempFile("collect-files-list-", ".txt");
      Files.writeString(localTempFile, fileListContent);

      try {
        // Upload file list to remote node (owned by linux_user; deleted below as the same user).
        String remoteFileListPath = remoteTmpDir + "/" + localTempFile.getFileName().toString();
        nodeUniverseManager.uploadFileToNode(
            node, universe, localTempFile.toString(), remoteFileListPath, "644", context);

        // Create tar inside the shared, yugabyte-owned staging directory. Every entry is
        // prefixed with the node name via --transform so that when the caller extracts each
        // per-node tar, the contents land in their own per-node subdirectory rather than
        // colliding on identical relative paths across nodes
        String remoteTarPath = collectionTarDir + "/" + tarFileName;
        List<String> cmd =
            List.of(
                "bash",
                "-c",
                String.format(
                    "cd %s && tar --warning=no-file-changed --transform='s,^,%s/,' -czhf %s -T %s"
                        + " 2>/dev/null; echo $?",
                    baseDir, node.nodeName, remoteTarPath, remoteFileListPath));

        ShellResponse response = nodeUniverseManager.runCommand(node, universe, cmd, context);

        // Clean up remote file list
        nodeUniverseManager.runCommand(
            node, universe, List.of("rm", "-f", remoteFileListPath), context);

        if (response.getCode() == 0) {
          // Verify tar file was created. Run the check as linux_user (via context) so it matches
          // the user that wrote the tar -- the staging dir is yugabyte-owned and yugabyte could
          // traverse it too, but using linux_user here keeps existence checks consistent with the
          // surrounding tar/chmod operations.
          if (nodeUniverseManager.checkNodeIfFileExists(node, universe, remoteTarPath, context)) {
            // Force the tar to be world-readable so the download path (default user) can read
            // it regardless of linux_user's umask. Must run as linux_user because only the file
            // owner can chmod.
            nodeUniverseManager.runCommand(
                node, universe, List.of("chmod", "0644", remoteTarPath), context);

            log.info(
                "Created tar file {} on node {} with {} files",
                remoteTarPath,
                node.nodeName,
                relativeFiles.size());
            return remoteTarPath;
          } else {
            log.warn(
                "Tar command succeeded but file {} not found on node {}",
                remoteTarPath,
                node.nodeName);
          }
        } else {
          log.error("Failed to create tar on node {}: {}", node.nodeName, response.getMessage());
        }
      } finally {
        // Clean up local temp file
        Files.deleteIfExists(localTempFile);
      }
    } catch (Exception e) {
      log.error("Error creating tar on node {}: {}", node.nodeName, e.getMessage(), e);
    }
    return null;
  }

  /**
   * Idempotently create the shared tar staging directory as the default Linux user. The directory
   * is set to mode 0777 (no sticky bit) so any impersonated user can write tars inside, while the
   * default user retains delete authority by virtue of owning the directory.
   */
  private void ensureCollectionTarDir(
      NodeDetails node, Universe universe, String collectionTarDir) {
    nodeUniverseManager.runCommand(
        node,
        universe,
        List.of(
            "bash",
            "-c",
            String.format("mkdir -p '%s' && chmod 0777 '%s'", collectionTarDir, collectionTarDir)),
        ShellProcessContext.DEFAULT);
  }
}
