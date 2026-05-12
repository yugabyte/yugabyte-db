// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.FileCollection;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.io.File;
import java.io.FileInputStream;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.Builder;
import lombok.Data;
import lombok.extern.slf4j.Slf4j;

/**
 * Service for downloading collected files from database nodes. Downloads tar archives created by
 * the createFileCollection API and streams them to the client.
 */
@Slf4j
@Singleton
public class FileCollectionDownloader {

  /** Maximum threads for parallel downloads */
  private static final int MAX_PARALLEL_THREADS = 50;

  /** Directory name for storing collected files */
  private static final String COLLECTED_FILES_DIR = "collected-files";

  /** Prefix for combined archive filenames */
  private static final String COLLECTED_FILES_PREFIX = "collected-files-";

  private final NodeUniverseManager nodeUniverseManager;
  private final PlatformExecutorFactory executorFactory;
  private final RuntimeConfGetter confGetter;

  @Inject
  public FileCollectionDownloader(
      NodeUniverseManager nodeUniverseManager,
      PlatformExecutorFactory executorFactory,
      RuntimeConfGetter confGetter) {
    this.nodeUniverseManager = nodeUniverseManager;
    this.executorFactory = executorFactory;
    this.confGetter = confGetter;
  }

  /** Result of downloading files from a single node */
  @Data
  @Builder
  public static class NodeDownloadResult {
    private String nodeName;
    private String nodeAddress;
    private boolean success;
    private String localPath;
    private long fileSizeBytes;
    private long downloadTimeMs;
    private String errorMessage;
  }

  /** Result of downloading files from all nodes */
  @Data
  @Builder
  public static class DownloadResult {
    private UUID collectionUuid;
    private int totalNodes;
    private int successfulNodes;
    private int failedNodes;
    private long totalBytesDownloaded;
    private long totalDownloadTimeMs;
    private boolean allSucceeded;
    private String outputDirectory;
    private Map<String, NodeDownloadResult> nodeResults;
  }

  /**
   * Download collected files from nodes and return as an InputStream. This method downloads files
   * from each node, combines them into a single archive, and returns an InputStream that can be
   * streamed directly to the HTTP client.
   *
   * @param collectionUuid The collection UUID from createFileCollection API
   * @param universe The universe (must match the one used for collection)
   * @return InputStream for the combined tar.gz archive
   */
  public InputStream downloadAsStream(UUID collectionUuid, Universe universe) {
    // Look up collection metadata from database
    FileCollection fileCollection = FileCollection.getOrBadRequest(collectionUuid);

    // Check if already cleaned up or DB nodes cleaned (can't download if source files are gone)
    if (fileCollection.isCleanedUp() || fileCollection.isDbNodesCleaned()) {
      throw new PlatformServiceException(
          400,
          "Collection "
              + collectionUuid
              + " files have been cleaned from DB nodes. Use POST /file-collections to create a"
              + " new collection.");
    }

    // Validate universe matches
    if (!fileCollection.getUniverseUuid().equals(universe.getUniverseUUID())) {
      throw new PlatformServiceException(
          400,
          "Collection "
              + collectionUuid
              + " was created for universe "
              + fileCollection.getUniverseUuid()
              + ", not "
              + universe.getUniverseUUID());
    }

    // Mark as downloading
    fileCollection.markDownloading();

    // Create temp directory for downloads:
    // {storagePath}/collected-files/{universeUuid}/{collectionUuid}
    String storagePath = AppConfigHelper.getStoragePath();
    String outputDirectory =
        Paths.get(
                storagePath,
                COLLECTED_FILES_DIR,
                universe.getUniverseUUID().toString(),
                collectionUuid.toString())
            .toString();
    Path outputPath = Paths.get(outputDirectory);
    try {
      Files.createDirectories(outputPath);
    } catch (Exception e) {
      fileCollection.markFailed();
      throw new PlatformServiceException(
          500, "Failed to create output directory: " + e.getMessage());
    }

    // Get node tar paths
    Map<String, String> nodeTarPaths = fileCollection.getNodeTarPaths();
    Set<String> nodeNames = nodeTarPaths.keySet();

    // Find target nodes (Set lookup is O(1) vs O(n) for list iteration)
    List<NodeDetails> targetNodes =
        universe.getNodes().stream()
            .filter(n -> nodeNames.contains(n.nodeName))
            .collect(Collectors.toList());

    // Log warning for nodes in collection but not found in universe
    Set<String> foundNodeNames =
        targetNodes.stream().map(n -> n.nodeName).collect(Collectors.toSet());
    nodeNames.stream()
        .filter(name -> !foundNodeNames.contains(name))
        .forEach(name -> log.warn("Node {} not found in universe, skipping download", name));

    if (targetNodes.isEmpty()) {
      fileCollection.markFailed();
      throw new PlatformServiceException(400, "No valid nodes found for collection");
    }

    // Get timeouts from config
    Duration downloadTimeout =
        confGetter.getGlobalConf(GlobalConfKeys.fileCollectionDownloadTimeout);
    Duration nodeDownloadTimeout =
        confGetter.getGlobalConf(GlobalConfKeys.fileCollectionNodeDownloadTimeout);

    // Download from each node in parallel
    Map<String, ParallelNodeExecutor.TaskOutcome<NodeDownloadResult>> outcomes =
        ParallelNodeExecutor.execute(
            targetNodes,
            node ->
                downloadFromNode(
                    universe,
                    node,
                    nodeTarPaths.get(node.nodeName),
                    outputDirectory,
                    nodeDownloadTimeout.toSeconds()),
            MAX_PARALLEL_THREADS,
            downloadTimeout.toSeconds(),
            executorFactory,
            "download-collection-stream");

    // Check for failures
    List<String> intermediateYbaFiles = new ArrayList<>();
    for (var entry : outcomes.entrySet()) {
      ParallelNodeExecutor.TaskOutcome<NodeDownloadResult> outcome = entry.getValue();
      if (outcome.getStatus() == ParallelNodeExecutor.TaskStatus.SUCCESS
          && outcome.getResult().isSuccess()) {
        intermediateYbaFiles.add(outcome.getResult().getLocalPath());
      } else {
        log.warn("Failed to download from node {}: {}", entry.getKey(), outcome.getErrorMessage());
      }
    }

    if (intermediateYbaFiles.isEmpty()) {
      fileCollection.markFailed();
      throw new PlatformServiceException(500, "Failed to download files from any node");
    }

    // Always combine into a single tar.gz for consistent, predictable output format
    String combinedTarPath =
        Paths.get(outputDirectory, COLLECTED_FILES_PREFIX + collectionUuid + ".tar.gz").toString();
    try {
      // Get just the filenames for the tar command
      List<String> tarArgs = new ArrayList<>();
      tarArgs.add("tar");
      tarArgs.add("--warning=no-file-changed");
      tarArgs.add("-czf");
      tarArgs.add(combinedTarPath);
      tarArgs.add("-C");
      tarArgs.add(outputDirectory);
      // Add each downloaded file by name (not full path since we use -C)
      for (String filePath : intermediateYbaFiles) {
        tarArgs.add(new File(filePath).getName());
      }

      log.debug("Running tar command: {}", String.join(" ", tarArgs));
      ProcessBuilder pb = new ProcessBuilder(tarArgs);
      pb.redirectErrorStream(true);
      Process process = pb.start();

      // Capture output for debugging
      String output =
          new String(
              process.getInputStream().readAllBytes(), java.nio.charset.StandardCharsets.UTF_8);
      int exitCode = process.waitFor();

      if (exitCode != 0) {
        log.error("Tar command failed with exit code {}: {}", exitCode, output);
        fileCollection.markFailed();
        throw new PlatformServiceException(
            500, "Failed to create combined archive: tar exit code " + exitCode + " - " + output);
      }

      File combinedFile = new File(combinedTarPath);
      if (!combinedFile.exists()) {
        log.error("Combined tar file was not created at {}", combinedTarPath);
        fileCollection.markFailed();
        throw new PlatformServiceException(500, "Combined archive file was not created");
      }

      log.info("Created combined archive {} ({} bytes)", combinedTarPath, combinedFile.length());

      fileCollection.markDownloaded(outputDirectory);
      return new FileInputStream(combinedFile);
    } catch (PlatformServiceException e) {
      throw e;
    } catch (Exception e) {
      log.error("Error creating combined archive: {}", e.getMessage(), e);
      fileCollection.markFailed();
      throw new PlatformServiceException(
          500, "Failed to create combined archive: " + e.getMessage());
    } finally {
      // Always clean up intermediate files (individual node tars) - idempotent if already deleted
      cleanupIntermediateYbaFiles(intermediateYbaFiles);
    }
  }

  /** Clean up intermediate YBA local files (individual node tars) after combining or on failure. */
  private void cleanupIntermediateYbaFiles(List<String> filePaths) {
    for (String filePath : filePaths) {
      try {
        File file = new File(filePath);
        if (file.exists() && file.delete()) {
          log.debug("Cleaned up intermediate file: {}", filePath);
        }
      } catch (Exception e) {
        log.warn("Failed to clean up intermediate file {}: {}", filePath, e.getMessage());
      }
    }
  }

  /**
   * Get the filename for a collection download.
   *
   * @param collectionUuid The collection UUID
   * @return Filename in format "collected-files-{uuid}.tar.gz"
   */
  public String getDownloadFileName(UUID collectionUuid) {
    return COLLECTED_FILES_PREFIX + collectionUuid.toString() + ".tar.gz";
  }

  /**
   * Clean up collected files from database nodes AND/OR local YBA storage.
   *
   * @param collectionUuid The collection UUID from createFileCollection API
   * @param universe The universe (must match the one used for collection)
   * @param deleteFromDbNodes Whether to delete tar files from DB nodes
   * @param deleteFromYba Whether to delete downloaded files from YBA local storage
   * @return Number of nodes successfully cleaned up (from DB nodes)
   */
  public int cleanupCollection(
      UUID collectionUuid, Universe universe, boolean deleteFromDbNodes, boolean deleteFromYba) {
    // Look up collection metadata from database
    FileCollection fileCollection = FileCollection.getOrBadRequest(collectionUuid);

    // Check if already fully cleaned up
    if (fileCollection.isCleanedUp()) {
      log.info("Collection {} already fully cleaned up", collectionUuid);
      return 0;
    }

    // Check if the requested locations are already cleaned
    if (deleteFromDbNodes && fileCollection.isDbNodesCleaned()) {
      log.info("Collection {} DB nodes already cleaned", collectionUuid);
      deleteFromDbNodes = false;
    }
    if (deleteFromYba && fileCollection.isYbaCleaned()) {
      log.info("Collection {} YBA local already cleaned", collectionUuid);
      deleteFromYba = false;
    }

    // Validate at least one cleanup option is selected and not already done
    if (!deleteFromDbNodes && !deleteFromYba) {
      log.info("Collection {} requested cleanup locations already cleaned", collectionUuid);
      return 0;
    }

    // Validate universe matches
    if (!fileCollection.getUniverseUuid().equals(universe.getUniverseUUID())) {
      throw new PlatformServiceException(
          400,
          "Collection "
              + collectionUuid
              + " was created for universe "
              + fileCollection.getUniverseUuid()
              + ", not "
              + universe.getUniverseUUID());
    }

    int cleanedCount = 0;

    // 1. Clean up remote tar files from DB nodes (if requested) - using parallel execution
    if (deleteFromDbNodes) {
      Map<String, String> nodeTarPaths = fileCollection.getNodeTarPaths();
      Set<String> nodeNames = nodeTarPaths.keySet();

      // Find target nodes (Set lookup is O(1) vs O(n) for list iteration)
      List<NodeDetails> targetNodes =
          universe.getNodes().stream()
              .filter(n -> nodeNames.contains(n.nodeName))
              .collect(Collectors.toList());

      // Log warning for nodes in collection but not found in universe
      Set<String> foundNodeNames =
          targetNodes.stream().map(n -> n.nodeName).collect(Collectors.toSet());
      nodeNames.stream()
          .filter(name -> !foundNodeNames.contains(name))
          .forEach(name -> log.warn("Node {} not found in universe, skipping cleanup", name));

      if (!targetNodes.isEmpty()) {
        // Get cleanup timeout from config
        Duration cleanupTimeout =
            confGetter.getGlobalConf(GlobalConfKeys.fileCollectionCleanupTimeout);

        // Execute cleanup in parallel
        Map<String, ParallelNodeExecutor.TaskOutcome<Boolean>> outcomes =
            ParallelNodeExecutor.execute(
                targetNodes,
                node -> cleanupNodeTarFile(universe, node, nodeTarPaths.get(node.nodeName)),
                MAX_PARALLEL_THREADS,
                cleanupTimeout.toSeconds(),
                executorFactory,
                "cleanup-collection");

        // Count successes
        for (var entry : outcomes.entrySet()) {
          ParallelNodeExecutor.TaskOutcome<Boolean> outcome = entry.getValue();
          if (outcome.getStatus() == ParallelNodeExecutor.TaskStatus.SUCCESS
              && outcome.getResult()) {
            cleanedCount++;
          } else {
            log.warn("Failed to clean up node {}: {}", entry.getKey(), outcome.getErrorMessage());
          }
        }
      }
    }

    // 2. Clean up local YBA download directory (if requested)
    if (deleteFromYba) {
      String localOutputPath = fileCollection.getOutputPath();
      if (localOutputPath != null) {
        cleanupLocalDirectory(localOutputPath, collectionUuid);
      }
    }

    // Update collection status based on what was cleaned
    if (deleteFromDbNodes && deleteFromYba) {
      // Both locations cleaned
      fileCollection.markCleanedUp();
    } else if (deleteFromDbNodes) {
      // Only DB nodes cleaned
      fileCollection.markDbNodesCleaned();
    } else if (deleteFromYba) {
      // Only YBA local cleaned
      fileCollection.markYbaCleaned();
    }

    log.info(
        "Cleaned up collection {} - DB nodes: {} (cleaned {}), YBA local: {}",
        collectionUuid,
        deleteFromDbNodes,
        cleanedCount,
        deleteFromYba);

    return cleanedCount;
  }

  /**
   * Clean up tar file from a single node.
   *
   * @param universe The universe
   * @param node The node to clean up
   * @param remoteTarPath Path to the tar file on the remote node
   * @return true if cleanup succeeded, false otherwise
   */
  private boolean cleanupNodeTarFile(Universe universe, NodeDetails node, String remoteTarPath) {
    try {
      if (remoteTarPath == null) {
        log.warn("No tar file path for node {}", node.nodeName);
        return false;
      }

      // Fail fast on unreachable nodes; otherwise rm-over-SSH will burn the full cleanup timeout
      // and surface as a generic "failed to clean up" instead of "node unreachable".
      if (!nodeUniverseManager.isNodeReachable(node, universe)) {
        log.warn(
            "Node {} is unreachable, skipping remote tar cleanup at {}",
            node.nodeName,
            remoteTarPath);
        return false;
      }

      nodeUniverseManager.runCommand(
          node, universe, List.of("rm", "-f", remoteTarPath), ShellProcessContext.DEFAULT);
      log.info("Cleaned up remote tar file {} on node {}", remoteTarPath, node.nodeName);
      return true;
    } catch (Exception e) {
      log.warn(
          "Failed to clean up remote tar file {} on node {}: {}",
          remoteTarPath,
          node.nodeName,
          e.getMessage());
      return false;
    }
  }

  /**
   * Delete a local directory and all its contents.
   *
   * @param directoryPath Path to the directory to delete
   * @param collectionUuid Collection UUID for logging
   */
  private void cleanupLocalDirectory(String directoryPath, UUID collectionUuid) {
    Path localPath = Paths.get(directoryPath);
    if (!Files.exists(localPath)) {
      log.debug("Local directory {} does not exist, skipping cleanup", directoryPath);
      return;
    }

    try {
      // Delete all files in the directory
      Files.walk(localPath)
          .sorted((a, b) -> b.compareTo(a)) // Reverse order to delete files before directories
          .forEach(
              path -> {
                try {
                  Files.delete(path);
                } catch (Exception e) {
                  log.warn("Failed to delete {}: {}", path, e.getMessage());
                }
              });
      log.info("Cleaned up local directory {} for collection {}", directoryPath, collectionUuid);
    } catch (Exception e) {
      log.warn(
          "Failed to fully clean up local directory {} for collection {}: {}",
          directoryPath,
          collectionUuid,
          e.getMessage());
    }
  }

  /**
   * Download a single tar file from a node.
   *
   * @param universe The universe
   * @param node The node to download from
   * @param remoteTarPath Path to the tar file on the remote node
   * @param outputDirectory Local directory to save the file
   * @param timeoutSecs Timeout for the download
   * @return Download result for this node
   */
  private NodeDownloadResult downloadFromNode(
      Universe universe,
      NodeDetails node,
      String remoteTarPath,
      String outputDirectory,
      long timeoutSecs) {
    long startTime = System.currentTimeMillis();

    try {
      if (remoteTarPath == null) {
        return NodeDownloadResult.builder()
            .nodeName(node.nodeName)
            .nodeAddress(node.cloudInfo.private_ip)
            .success(false)
            .errorMessage("No tar file path available for this node")
            .downloadTimeMs(System.currentTimeMillis() - startTime)
            .build();
      }

      // Fail fast on unreachable nodes; otherwise the per-node probes below swallow the
      // connection error and surface as a misleading "Remote tar file not found".
      if (!nodeUniverseManager.isNodeReachable(node, universe)) {
        return NodeDownloadResult.builder()
            .nodeName(node.nodeName)
            .nodeAddress(node.cloudInfo.private_ip)
            .success(false)
            .errorMessage("Node is unreachable")
            .downloadTimeMs(System.currentTimeMillis() - startTime)
            .build();
      }

      // Check if remote file exists
      if (!nodeUniverseManager.checkNodeIfFileExists(node, universe, remoteTarPath)) {
        return NodeDownloadResult.builder()
            .nodeName(node.nodeName)
            .nodeAddress(node.cloudInfo.private_ip)
            .success(false)
            .errorMessage("Remote tar file not found: " + remoteTarPath)
            .downloadTimeMs(System.currentTimeMillis() - startTime)
            .build();
      }

      // Determine local file path
      String fileName = new File(remoteTarPath).getName();
      Path localPath = Paths.get(outputDirectory, fileName);

      // Download the file using NodeUniverseManager
      nodeUniverseManager.downloadNodeFile(
          node, universe, "/", List.of(remoteTarPath), localPath.toString());

      // Verify download
      if (Files.exists(localPath)) {
        long downloadedSize = Files.size(localPath);
        long downloadTime = System.currentTimeMillis() - startTime;

        log.info(
            "Downloaded {} from node {} ({} bytes in {}ms)",
            fileName,
            node.nodeName,
            downloadedSize,
            downloadTime);

        // Note: Remote tar files are NOT auto-deleted here.
        // Use the deleteFileCollection API to remove remote files when done.

        return NodeDownloadResult.builder()
            .nodeName(node.nodeName)
            .nodeAddress(node.cloudInfo.private_ip)
            .success(true)
            .localPath(localPath.toString())
            .fileSizeBytes(downloadedSize)
            .downloadTimeMs(downloadTime)
            .build();
      }

      return NodeDownloadResult.builder()
          .nodeName(node.nodeName)
          .nodeAddress(node.cloudInfo.private_ip)
          .success(false)
          .errorMessage("Download failed - local file not created")
          .downloadTimeMs(System.currentTimeMillis() - startTime)
          .build();

    } catch (Exception e) {
      log.error("Error downloading from node {}: {}", node.nodeName, e.getMessage(), e);
      return NodeDownloadResult.builder()
          .nodeName(node.nodeName)
          .nodeAddress(node.cloudInfo.private_ip)
          .success(false)
          .errorMessage(e.getMessage())
          .downloadTimeMs(System.currentTimeMillis() - startTime)
          .build();
    }
  }
}
