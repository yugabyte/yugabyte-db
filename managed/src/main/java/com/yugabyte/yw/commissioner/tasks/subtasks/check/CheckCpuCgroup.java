// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks.check;

import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ShellProcessContext;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.Collections;
import java.util.List;
import java.util.Set;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;

/**
 * Precheck task that verifies the CPU cgroup required for YugabyteDB is configured on on-prem
 * nodes. Since this runs against a live universe, the yb-tserver process must already own the
 * {@code yugabyte-db} cgroup. The check is uniform across cgroup v1 and v2: it inspects {@code
 * /proc/<tserver-pid>/cgroup} to verify membership in a {@code yugabyte-db} cgroup and confirms the
 * yugabyte user can create child cgroups inside that directory (required for multi-tenancy QoS,
 * which spawns per-tenant sub-cgroups at runtime).
 *
 * <p>The bash implementation is shipped as a node-agent template at {@code
 * managed/node-agent/resources/templates/server/check_cpu_cgroup.sh.j2} and copied to {@code
 * <yb_home>/bin/check_cpu_cgroup.sh} by the node-agent {@code ConfigureServer} step when {@code
 * configure_cgroup} is enabled (see {@code CgroupScriptFilesToCopy} in {@code
 * configure_server.go}). If the script is missing on a node, this precheck fails with an actionable
 * error message.
 */
@Slf4j
public class CheckCpuCgroup extends UniverseTaskBase {

  private static final String CHECK_SCRIPT_NAME = "check_cpu_cgroup.sh";

  @Inject
  protected CheckCpuCgroup(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends UniverseTaskParams {
    public Set<String> nodeNames;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    Universe universe = getUniverse();
    if (CollectionUtils.isEmpty(taskParams().nodeNames)) {
      log.info("No nodes were provided for CPU cgroup validation.");
      return;
    }
    List<NodeDetails> nodes =
        universe.getNodes().stream()
            .filter(n -> taskParams().nodeNames.contains(n.nodeName))
            .collect(Collectors.toList());

    long timeoutSecs =
        confGetter.getConfForScope(universe, UniverseConfKeys.checkCpuCgroupTimeoutSecs);
    ShellProcessContext context =
        ShellProcessContext.builder()
            .traceLogging(true)
            .logCmdOutput(true)
            .timeoutSecs(timeoutSecs)
            .build();

    for (NodeDetails node : nodes) {
      runOnNode(node, universe, context);
    }
  }

  private void runOnNode(NodeDetails node, Universe universe, ShellProcessContext context) {
    String remoteScriptPath =
        nodeUniverseManager.getYbHomeDir(node, universe) + "/bin/" + CHECK_SCRIPT_NAME;

    // The node-agent ConfigureServer step only ships the script when configure_cgroup is enabled
    // (CgroupScriptFilesToCopy in configure_server.go). If it's missing, the universe was never
    // configured for cgroups and the precheck cannot proceed.
    boolean scriptPresent;
    try {
      scriptPresent = nodeUniverseManager.checkNodeIfFileExists(node, universe, remoteScriptPath);
    } catch (Exception e) {
      log.error(
          "Failed to verify CPU cgroup precheck script presence on node {}", node.nodeName, e);
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR,
          "Failed to verify CPU cgroup precheck script on node "
              + node.nodeName
              + ": "
              + e.getMessage());
    }
    if (!scriptPresent) {
      String msg =
          "CPU cgroup precheck script not found at "
              + remoteScriptPath
              + " on node "
              + node.nodeName
              + "; ensure node-agent ConfigureServer ran with configure_cgroup=true on this node";
      log.error(msg);
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, msg);
    }

    log.info("Running CPU cgroup precheck on node {} via {}", node.nodeName, remoteScriptPath);
    ShellResponse response;
    try {
      response =
          nodeUniverseManager.runCommand(
              node,
              universe,
              Collections.singletonList(remoteScriptPath),
              context,
              false /* useBash */);
    } catch (Exception e) {
      log.error("CPU cgroup precheck errored on node {}", node.nodeName, e);
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR,
          "CPU cgroup precheck errored on node " + node.nodeName + ": " + e.getMessage());
    }
    if (response.code != 0) {
      String msg = response.message == null ? "<no output>" : response.message.trim();
      log.error("CPU cgroup precheck failed on node {}: {}", node.nodeName, msg);
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR,
          "CPU cgroup precheck failed on node " + node.nodeName + ": " + msg);
    }
    String output = response.extractRunCommandOutput();
    if (output == null || !output.contains("OK:")) {
      log.error(
          "CPU cgroup precheck returned unexpected output on node {}: {}", node.nodeName, output);
      throw new PlatformServiceException(
          INTERNAL_SERVER_ERROR,
          "CPU cgroup precheck failed on node "
              + node.nodeName
              + ": "
              + (output == null ? "<no output>" : output));
    }
    log.info("CPU cgroup precheck passed on node {}: {}", node.nodeName, output);
  }
}
