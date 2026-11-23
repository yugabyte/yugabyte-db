// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundlev2;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.ShellProcessContext;
import com.yugabyte.yw.common.ShellProcessHandler;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.supportbundlev2.cgroup.SupportBundleCgroupScriptExecutor;
import com.yugabyte.yw.common.supportbundlev2.manifest.SupportBundleV2ManifestSpecBuilder;
import com.yugabyte.yw.forms.SupportBundleFormData;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.ScriptTarComponentSpec;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.time.Duration;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Date;
import java.util.List;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;

/**
 * Runs support bundle v2 estimate bash entrypoints on DB nodes or the YBA host.
 *
 * <p>Estimation reuses the collection scripts rather than reimplementing their file selection: the
 * manifest names a {@code create_*_bundle} entrypoint, and this class swaps it for the matching
 * {@code estimate_*_size} one so the same filters, date window and gflags decide what is counted.
 * An estimate that disagreed with collection would be worse than no estimate.
 *
 * <p><b>Nothing here throws.</b> Every failure path logs and returns {@code 0}, because an estimate
 * is advisory and must not fail the API call that asked for it. The cost is that a failed estimate
 * and a genuinely empty component both report zero -- {@link
 * SupportBundleV2EstimateStdoutParser#parseEstimateSize} distinguishes them, but that distinction
 * is collapsed at this boundary.
 */
@Singleton
@Slf4j
public class SupportBundleScriptEstimateSupport {

  private static final Duration MIN_SCRIPT_TIMEOUT = Duration.ofSeconds(1);

  private final Config staticConf;
  private final RuntimeConfGetter confGetter;
  private final NodeUniverseManager nodeUniverseManager;
  private final SupportBundleRunAsSupport runAsSupport;
  private final SupportBundleCgroupScriptExecutor cgroupScriptExecutor;
  private final ShellProcessHandler shellProcessHandler;

  @Inject
  public SupportBundleScriptEstimateSupport(
      Config staticConf,
      RuntimeConfGetter confGetter,
      NodeUniverseManager nodeUniverseManager,
      SupportBundleRunAsSupport runAsSupport,
      SupportBundleCgroupScriptExecutor cgroupScriptExecutor,
      ShellProcessHandler shellProcessHandler) {
    this.staticConf = staticConf;
    this.confGetter = confGetter;
    this.nodeUniverseManager = nodeUniverseManager;
    this.runAsSupport = runAsSupport;
    this.cgroupScriptExecutor = cgroupScriptExecutor;
    this.shellProcessHandler = shellProcessHandler;
  }

  /** The spec's timeout, capped by {@code estimate_script_timeout}. */
  private long estimateTimeoutSecs(ScriptTarComponentSpec spec) {
    long specTimeoutSecs =
        spec.getTimeoutSecs() > 0
            ? spec.getTimeoutSecs()
            : SupportBundleV2ManifestSpecBuilder.DEFAULT_TIMEOUT_SECS;
    return Math.min(specTimeoutSecs, estimateScriptTimeout(confGetter).getSeconds());
  }

  /** {@code estimate_script_timeout}, at least a second: shorter would become "no timeout". */
  static Duration estimateScriptTimeout(RuntimeConfGetter confGetter) {
    Duration configured =
        confGetter.getGlobalConf(GlobalConfKeys.supportBundleEstimateScriptTimeout);
    return configured.compareTo(MIN_SCRIPT_TIMEOUT) < 0 ? MIN_SCRIPT_TIMEOUT : configured;
  }

  /**
   * Runs one component's estimate on a DB node. Mirrors the collect path deliberately -- same token
   * substitution, same param resolution, same run user from the manifest -- so the two cannot drift
   * into measuring different things.
   */
  public long runNodeScriptEstimate(
      ScriptTarComponentSpec spec,
      String componentName,
      SupportBundleFormData bundleData,
      Universe universe,
      NodeDetails node,
      Date startDate,
      Date endDate,
      UUID stagingId) {
    try {
      String nodeName = node.getNodeName();
      List<String> params = substituteNodeName(spec.getParams(), nodeName);
      params =
          SupportBundleScriptParamResolver.resolveParams(
              params,
              componentName,
              universe,
              node,
              nodeUniverseManager,
              bundleData,
              startDate,
              endDate,
              staticConf,
              confGetter);
      params = replaceCreateEntrypointWithEstimate(params, componentName);

      SupportBundleRunAsSupport.SpecContexts contexts =
          runAsSupport.contextsFor(spec, componentName, universe, node, estimateTimeoutSecs(spec));

      ShellResponse response =
          executeNodeScript(spec, componentName, universe, node, params, contexts, stagingId);
      if (response.getCode() != 0) {
        log.error(
            "Estimate script failed on node '{}' for component '{}': exit={}, output={}",
            nodeName,
            componentName,
            response.getCode(),
            response.getMessage());
        return 0L;
      }
      return SupportBundleV2EstimateStdoutParser.parseEstimateSize(response.getMessage())
          .orElse(0L);
    } catch (Exception e) {
      SupportBundleCancellation.rethrowIfCancelled(
          e, String.format("the estimate of '%s' on node '%s'", componentName, node.getNodeName()));
      log.error(
          "Error running node estimate script for component '{}' on node '{}'",
          componentName,
          node.getNodeName(),
          e);
      return 0L;
    }
  }

  /**
   * The YBA-host counterpart. Runs the script locally through {@link ShellProcessHandler} instead
   * of over SSH, and checks the script exists first: a local path that is wrong is a packaging bug
   * worth an explicit message, not a shell exit code to decode.
   */
  public long runYbaScriptEstimate(
      ScriptTarComponentSpec spec,
      String componentName,
      Universe universe,
      SupportBundleFormData bundleData,
      Date startDate,
      Date endDate) {
    try {
      List<String> params =
          spec.getParams() == null ? new ArrayList<>() : new ArrayList<>(spec.getParams());
      params =
          SupportBundleScriptParamResolver.resolveYbaParams(
              params,
              componentName,
              universe,
              bundleData,
              startDate,
              endDate,
              staticConf,
              confGetter);
      params = replaceCreateEntrypointWithEstimate(params, componentName);

      ShellProcessContext context =
          ShellProcessContext.builder()
              .logCmdOutput(true)
              .timeoutSecs(estimateTimeoutSecs(spec))
              .build();

      Path scriptPath = resolveScriptPath(spec.getScriptPath());
      if (!Files.isRegularFile(scriptPath)) {
        throw new RuntimeException(
            String.format(
                "YBA estimate script not found at '%s' (resolved from '%s').",
                scriptPath.toAbsolutePath(), spec.getScriptPath()));
      }
      List<String> command = new ArrayList<>();
      command.add("/bin/bash");
      command.add(scriptPath.toAbsolutePath().toString());
      command.addAll(params);
      ShellResponse response =
          SupportBundleCancellation.throwIfCancelled(shellProcessHandler.run(command, context));
      if (response.getCode() != 0) {
        log.error(
            "Estimate script failed on YBA host for component '{}': exit={}, output={}",
            componentName,
            response.getCode(),
            response.getMessage());
        return 0L;
      }
      return SupportBundleV2EstimateStdoutParser.parseEstimateSize(response.getMessage())
          .orElse(0L);
    } catch (Exception e) {
      SupportBundleCancellation.rethrowIfCancelled(
          e, String.format("the estimate of '%s' on YBA host", componentName));
      log.error("Error running YBA estimate script for component '{}'", componentName, e);
      return 0L;
    }
  }

  /** Removes an estimate's staged copies; see {@link #runNodeScriptEstimate}. */
  public void cleanupStaging(UUID stagingId, Universe universe) {
    cgroupScriptExecutor.cleanupStaging(stagingId, universe);
  }

  /**
   * Runs from the staged script like collection does, but never under limits: an estimate only
   * measures. Staged as the spec's user, as collection stages it.
   */
  private ShellResponse executeNodeScript(
      ScriptTarComponentSpec spec,
      String componentName,
      Universe universe,
      NodeDetails node,
      List<String> params,
      SupportBundleRunAsSupport.SpecContexts contexts,
      UUID stagingId) {
    Path scriptPath = resolveScriptPath(spec.getScriptPath());
    return cgroupScriptExecutor.executeScript(
        stagingId,
        universe,
        node,
        spec.getScriptPath(),
        scriptPath,
        params,
        contexts.run(),
        contexts.setup(),
        componentName,
        false /* applyLimits */);
  }

  /**
   * The entrypoint is the first param by manifest convention, so only element 0 is rewritten and
   * the rest are passed through untouched. Falls back to deriving the name from the component when
   * the spec carries no params at all, or when the create entrypoint does not fit the naming rule.
   */
  private List<String> replaceCreateEntrypointWithEstimate(
      List<String> params, String componentName) {
    List<String> estimateParams = new ArrayList<>(params == null ? List.of() : params);
    if (estimateParams.isEmpty()) {
      estimateParams.add(
          SupportBundleV2EstimateRegistry.toEstimateEntrypointFromComponentName(componentName));
      return estimateParams;
    }
    String createEntry = estimateParams.get(0);
    String estimateEntry =
        SupportBundleV2EstimateRegistry.toEstimateEntrypoint(createEntry)
            .orElse(
                SupportBundleV2EstimateRegistry.toEstimateEntrypointFromComponentName(
                    componentName));
    estimateParams.set(0, estimateEntry);
    return estimateParams;
  }

  private Path resolveScriptPath(String scriptPath) {
    Path path = Paths.get(scriptPath);
    if (!path.isAbsolute()) {
      path = Paths.get(staticConf.getString("yb.devops.home"), scriptPath);
    }
    return path;
  }

  private List<String> substituteNodeName(List<String> params, String nodeName) {
    if (params == null) {
      return Collections.emptyList();
    }
    return params.stream().map(p -> substituteNodeName(p, nodeName)).collect(Collectors.toList());
  }

  /** Null bundle UUID: there is no bundle at estimate time, so that token cannot be expanded. */
  private String substituteNodeName(String value, String nodeName) {
    return SupportBundleSpecTokenSupport.substituteSpecTokens(value, nodeName, null);
  }
}
