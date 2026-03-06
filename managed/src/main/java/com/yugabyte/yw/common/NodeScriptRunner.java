// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.Builder;
import lombok.Data;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

/**
 * Service for running scripts on multiple nodes in a universe synchronously and collecting the
 * results.
 */
@Slf4j
@Singleton
public class NodeScriptRunner {

  /** Maximum threads for parallel script execution to prevent OOM/thrashing */
  private static final int MAX_PARALLEL_THREADS = 50;

  private final NodeUniverseManager nodeUniverseManager;
  private final PlatformExecutorFactory executorFactory;

  @Inject
  public NodeScriptRunner(
      NodeUniverseManager nodeUniverseManager, PlatformExecutorFactory executorFactory) {
    this.nodeUniverseManager = nodeUniverseManager;
    this.executorFactory = executorFactory;
  }

  /** Parameters for script execution */
  @Data
  @Builder
  public static class ScriptParams {
    private String scriptContent;
    private String scriptFile;
    private List<String> params;
    private long timeoutSecs;
    private String linuxUser;
  }

  /** Node selection criteria */
  @Data
  @Builder
  public static class NodeFilter {
    private List<String> nodeNames;
    private UUID clusterUuid;
    @Builder.Default private Boolean mastersOnly = false;
    @Builder.Default private Boolean tserversOnly = false;
    @Builder.Default private int maxParallelNodes = MAX_PARALLEL_THREADS;
  }

  /** Result from a single node */
  @Data
  @Builder
  public static class NodeResult {
    private String nodeName;
    private String nodeAddress;
    private int exitCode;
    private String stdout;
    private long executionTimeMs;
    private boolean success;
    private String errorMessage;
  }

  /** Aggregated results from all nodes */
  @Data
  @Builder
  public static class ExecutionResult {
    private int totalNodes;
    private int successfulNodes;
    private int failedNodes;
    private long totalExecutionTimeMs;
    private boolean allSucceeded;
    private Map<String, NodeResult> nodeResults;
  }

  /**
   * Run a script on selected nodes in a universe and return results.
   *
   * @param universe The universe to run the script on
   * @param scriptParams Script configuration
   * @param nodeFilter Optional node selection criteria (null for all nodes)
   * @return Aggregated execution results
   */
  public ExecutionResult runScript(
      Universe universe, ScriptParams scriptParams, NodeFilter nodeFilter) {
    // Validate linux_user for manual on-prem providers
    Util.validateLinuxUserForOnPrem(scriptParams.getLinuxUser(), universe);

    List<NodeDetails> targetNodes = getTargetNodes(universe, nodeFilter);

    if (targetNodes.isEmpty()) {
      return ExecutionResult.builder()
          .totalNodes(0)
          .successfulNodes(0)
          .failedNodes(0)
          .allSucceeded(true)
          .nodeResults(new LinkedHashMap<>())
          .build();
    }

    long startTime = System.currentTimeMillis();

    // Run script on all target nodes in parallel using shared executor utility
    int maxParallelNodes =
        (nodeFilter != null && nodeFilter.getMaxParallelNodes() > 0)
            ? nodeFilter.getMaxParallelNodes()
            : MAX_PARALLEL_THREADS;
    long waitTimeoutSecs = scriptParams.getTimeoutSecs() + 30;

    Map<String, ParallelNodeExecutor.TaskOutcome<NodeResult>> outcomes =
        ParallelNodeExecutor.execute(
            targetNodes,
            node -> executeOnNode(universe, node, scriptParams),
            maxParallelNodes,
            waitTimeoutSecs,
            executorFactory,
            "run-script");

    // Convert task outcomes to NodeResults
    Map<String, NodeResult> results = new LinkedHashMap<>();
    int successCount = 0;
    int failCount = 0;

    for (var entry : outcomes.entrySet()) {
      String nodeName = entry.getKey();
      ParallelNodeExecutor.TaskOutcome<NodeResult> outcome = entry.getValue();

      if (outcome.getStatus() == ParallelNodeExecutor.TaskStatus.SUCCESS) {
        results.put(nodeName, outcome.getResult());
        if (outcome.getResult().isSuccess()) {
          successCount++;
        } else {
          failCount++;
        }
      } else {
        // Build error result for non-success outcomes
        results.put(
            nodeName,
            NodeResult.builder()
                .nodeName(nodeName)
                .nodeAddress(outcome.getNode().cloudInfo.private_ip)
                .exitCode(-1)
                .stdout("")
                .errorMessage(outcome.getErrorMessage())
                .executionTimeMs(outcome.getExecutionTimeMs())
                .success(false)
                .build());
        failCount++;
      }
    }

    long totalTime = System.currentTimeMillis() - startTime;

    return ExecutionResult.builder()
        .totalNodes(targetNodes.size())
        .successfulNodes(successCount)
        .failedNodes(failCount)
        .totalExecutionTimeMs(totalTime)
        .allSucceeded(failCount == 0)
        .nodeResults(results)
        .build();
  }

  private NodeResult executeOnNode(Universe universe, NodeDetails node, ScriptParams scriptParams) {
    long nodeStartTime = System.currentTimeMillis();

    try {
      ShellProcessContext.ShellProcessContextBuilder contextBuilder =
          ShellProcessContext.builder()
              .logCmdOutput(true)
              .timeoutSecs(scriptParams.getTimeoutSecs());
      // Set the user to run the script as (defaults to yugabyte if not specified)
      if (StringUtils.isNotBlank(scriptParams.getLinuxUser())) {
        contextBuilder.sshUser(scriptParams.getLinuxUser());
      }
      ShellProcessContext context = contextBuilder.build();

      ShellResponse response;
      List<String> params =
          scriptParams.getParams() != null ? scriptParams.getParams() : new ArrayList<>();

      if (StringUtils.isNotBlank(scriptParams.getScriptFile())) {
        // Run script from file path using runScript which handles Node Agent and SSH fallback
        response =
            nodeUniverseManager.runScript(
                node, universe, scriptParams.getScriptFile(), params, context);
      } else {
        // Run inline script content - we construct the bash command ourselves
        // to avoid the double-quoting issue in getBashCommand when script contains spaces
        StringBuilder scriptCmd = new StringBuilder(scriptParams.getScriptContent());
        if (!params.isEmpty()) {
          scriptCmd.append(" ").append(String.join(" ", params));
        }
        List<String> cmd = Arrays.asList("bash", "-c", scriptCmd.toString());
        response = nodeUniverseManager.runCommand(node, universe, cmd, context, false);
      }

      long executionTime = System.currentTimeMillis() - nodeStartTime;

      return NodeResult.builder()
          .nodeName(node.nodeName)
          .nodeAddress(node.cloudInfo.private_ip)
          .exitCode(response.getCode())
          .stdout(response.getMessage())
          .executionTimeMs(executionTime)
          .success(response.getCode() == 0)
          .build();

    } catch (Exception e) {
      long executionTime = System.currentTimeMillis() - nodeStartTime;
      log.error("Error executing script on node {}: {}", node.nodeName, e.getMessage());

      return NodeResult.builder()
          .nodeName(node.nodeName)
          .nodeAddress(node.cloudInfo.private_ip)
          .exitCode(-1)
          .executionTimeMs(executionTime)
          .success(false)
          .errorMessage(e.getMessage())
          .build();
    }
  }

  /**
   * Get target nodes from a universe based on filter criteria. This method is shared across
   * multiple node operation services (runScript, collectFiles, etc.).
   *
   * @param universe The universe to get nodes from
   * @param nodeFilter Optional filter criteria (null returns all live nodes)
   * @return List of matching live nodes
   */
  public static List<NodeDetails> getTargetNodes(Universe universe, NodeFilter nodeFilter) {
    List<NodeDetails> nodes =
        universe.getUniverseDetails().nodeDetailsSet.stream()
            .filter(n -> n.state == NodeDetails.NodeState.Live)
            .collect(Collectors.toList());

    if (nodeFilter == null) {
      return nodes;
    }

    // Filter by node names
    if (nodeFilter.getNodeNames() != null && !nodeFilter.getNodeNames().isEmpty()) {
      Set<String> nameSet = new HashSet<>(nodeFilter.getNodeNames());
      nodes = nodes.stream().filter(n -> nameSet.contains(n.nodeName)).collect(Collectors.toList());
    }

    // Filter by cluster
    if (nodeFilter.getClusterUuid() != null) {
      UUID clusterUuid = nodeFilter.getClusterUuid();
      nodes =
          nodes.stream()
              .filter(n -> clusterUuid.equals(n.placementUuid))
              .collect(Collectors.toList());
    }

    // Filter by role
    boolean mastersOnly = Boolean.TRUE.equals(nodeFilter.getMastersOnly());
    boolean tserversOnly = Boolean.TRUE.equals(nodeFilter.getTserversOnly());
    if (mastersOnly && !tserversOnly) {
      nodes = nodes.stream().filter(n -> n.isMaster).collect(Collectors.toList());
    } else if (tserversOnly && !mastersOnly) {
      nodes = nodes.stream().filter(n -> n.isTserver).collect(Collectors.toList());
    }

    return nodes;
  }
}
