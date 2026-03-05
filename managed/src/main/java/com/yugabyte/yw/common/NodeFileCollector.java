// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.NodeScriptRunner.NodeFilter;
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

/**
 * Service for collecting files from multiple nodes in a universe synchronously and returning the
 * results. Reuses existing support bundle and node-agent infrastructure for file downloads.
 */
@Slf4j
@Singleton
public class NodeFileCollector {

  /** Maximum threads for parallel file collection to prevent OOM/thrashing */
  private static final int MAX_PARALLEL_THREADS = 50;

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
   * infrastructure for file downloads via Node Agent or SSH.
   *
   * @param universe The universe to collect files from
   * @param collectionParams File collection configuration
   * @param nodeFilter Optional node selection criteria (null for all nodes)
   * @return Aggregated collection results
   */
  public CollectionResult collectFiles(
      Universe universe, CollectionParams collectionParams, NodeFilter nodeFilter) {
    // Validate linux_user for manual on-prem providers
    Util.validateLinuxUserForOnPrem(collectionParams.getLinuxUser(), universe);

    List<NodeDetails> targetNodes = NodeScriptRunner.getTargetNodes(universe, nodeFilter);

    if (targetNodes.isEmpty()) {
      return CollectionResult.builder()
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

    return CollectionResult.builder()
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

    try {
      // Collect all file paths to tar
      List<String> filesToCollect = new ArrayList<>();
      Map<String, Long> fileSizeMap = new LinkedHashMap<>();

      // Add individual file paths - check existence and size before download
      if (params.getFilePaths() != null) {
        for (String filePath : params.getFilePaths()) {
          if (nodeUniverseManager.checkNodeIfFileExists(node, universe, filePath)) {
            // Get file size using stat command
            long fileSize = getRemoteFileSize(node, universe, filePath);

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
          } else {
            fileResults.add(
                FileResult.builder()
                    .remotePath(filePath)
                    .success(false)
                    .errorMessage("File not found")
                    .build());
            failed++;
          }
        }
      }

      // If directory paths are specified, list files in those directories
      // Reuses NodeUniverseManager.getNodeFilePathAndSizes() from support bundle code
      if (params.getDirectoryPaths() != null) {
        for (String dirPath : params.getDirectoryPaths()) {
          try {
            // Resolve symlinks to get the real path (like support bundle does)
            String resolvedDirPath = resolveSymlink(node, universe, dirPath);
            Map<String, Long> filesInDir =
                nodeUniverseManager.getNodeFilePathAndSizes(
                    node, universe, resolvedDirPath, params.getMaxDepth(), "f");
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
            .success(true)
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
        remoteTarPath = createTarOnRemoteNode(node, universe, "/", relativeFiles, tarFileName);

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
          .success(true)
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
   * Get the size of a single file on a remote node using stat command. This is more efficient than
   * listing the entire parent directory.
   *
   * @param node The target node
   * @param universe The universe
   * @param filePath The absolute path to the file
   * @return File size in bytes, or 0 if unable to determine
   */
  private long getRemoteFileSize(NodeDetails node, Universe universe, String filePath) {
    try {
      // Use stat to get file size - format %s gives size in bytes
      // Using -L to follow symlinks (e.g., yb-tserver.INFO -> yb-tserver.INFO.timestamp)
      List<String> cmd = List.of("stat", "-L", "-c", "%s", filePath);
      ShellResponse response =
          nodeUniverseManager.runCommand(node, universe, cmd, ShellProcessContext.DEFAULT);

      if (response.getCode() == 0 && response.getMessage() != null) {
        // Use extractRunCommandOutput to strip "Command output:" prefix
        String output = response.extractRunCommandOutput();
        return Long.parseLong(output);
      }
    } catch (Exception e) {
      log.warn(
          "Failed to get file size for {} on node {}: {}", filePath, node.nodeName, e.getMessage());
    }
    return 0L;
  }

  /**
   * Resolve symlinks on a remote node path using readlink -f. This ensures we use the real path
   * when listing files, similar to how support bundle uses actual mount paths.
   *
   * @param node The target node
   * @param universe The universe
   * @param path The path that might be a symlink
   * @return The resolved real path, or original path if resolution fails
   */
  private String resolveSymlink(NodeDetails node, Universe universe, String path) {
    try {
      List<String> cmd = List.of("readlink", "-f", path);
      ShellResponse response =
          nodeUniverseManager.runCommand(node, universe, cmd, ShellProcessContext.DEFAULT);

      if (response.getCode() == 0 && response.getMessage() != null) {
        // Use extractRunCommandOutput to strip "Command output:" prefix
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
   * Create a tar.gz archive on the remote node containing the specified files. The tar file is
   * created in the remote node's tmp directory and NOT downloaded to YBA. A separate API can be
   * used to download the tar file later.
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
      String tarFileName) {
    try {
      // Get remote tmp directory
      String remoteTmpDir = nodeUniverseManager.getRemoteTmpDir(node, universe);

      // Create file list content
      String fileListContent = String.join("\n", relativeFiles);

      // Create a temporary file locally with the file list
      Path localTempFile = Files.createTempFile("collect-files-list-", ".txt");
      Files.writeString(localTempFile, fileListContent);

      try {
        // Upload file list to remote node
        String remoteFileListPath = remoteTmpDir + "/" + localTempFile.getFileName().toString();
        nodeUniverseManager.uploadFileToNode(
            node, universe, localTempFile.toString(), remoteFileListPath, "644");

        // Run node_utils.sh create_tar_file on the remote node
        // Parameters: change_to_dir, tar_file_name, file_list_text_path
        String remoteTarPath = remoteTmpDir + "/" + tarFileName;
        List<String> cmd =
            List.of(
                "bash",
                "-c",
                String.format(
                    "cd %s && tar -czhf %s -T %s 2>/dev/null; echo $?",
                    baseDir, remoteTarPath, remoteFileListPath));

        ShellResponse response =
            nodeUniverseManager.runCommand(node, universe, cmd, ShellProcessContext.DEFAULT);

        // Clean up remote file list
        nodeUniverseManager.runCommand(
            node, universe, List.of("rm", "-f", remoteFileListPath), ShellProcessContext.DEFAULT);

        if (response.getCode() == 0) {
          // Verify tar file was created
          if (nodeUniverseManager.checkNodeIfFileExists(node, universe, remoteTarPath)) {
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
}
