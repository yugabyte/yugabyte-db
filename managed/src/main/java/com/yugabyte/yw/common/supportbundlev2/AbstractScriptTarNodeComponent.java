// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundlev2;

import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.tasks.params.SupportBundleTaskParams;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.ShellProcessContext;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.supportbundle.SupportBundleComponent;
import com.yugabyte.yw.common.supportbundlev2.cgroup.SupportBundleCgroupScriptExecutor;
import com.yugabyte.yw.common.supportbundlev2.manifest.SupportBundleV2ManifestSpecBuilder;
import com.yugabyte.yw.common.supportbundlev2.nodebin.SupportBundleCdcStateDumpSupport;
import com.yugabyte.yw.common.supportbundlev2.nodebin.SupportBundleLogParseSupport;
import com.yugabyte.yw.common.supportbundlev2.nodebin.SupportBundleServerConfRedactionSupport;
import com.yugabyte.yw.forms.SupportBundleFormData;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.BundleDetails;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.ScriptTarComponentSpec;
import java.io.File;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Arrays;
import java.util.Date;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

/**
 * Base class for node-level components that run a bash script on a DB node, copy back the tar it
 * produces, and untar it into that node's bundle directory.
 *
 * <p><b>Stdout marker protocol.</b> The entrypoints in {@code devops/bin/node_utils.sh} report
 * their outcome as {@code KEY=value} lines on stdout, because the exit code alone is ambiguous:
 * "ran fine and matched no files" is a success, not a failure. {@link #extractScriptMarkers} reads
 * three keys, each with a fail-safe default:
 *
 * <ul>
 *   <li>{@code BUNDLE_STATUS} - one of {@code OK}, {@code EMPTY}, {@code FAILED}. Absent is read as
 *       {@code FAILED}, so a script killed before it prints anything is never mistaken for an empty
 *       collection.
 *   <li>{@code BUNDLE_PATH} - only checked against the {@code remoteTarPath} YBA asked for; YBA
 *       never copies or deletes any other path.
 *   <li>{@code REDACTION_STATUS} - {@code NODE}, {@code FAILED} or {@code NOT_APPLICABLE}, from
 *       components YBA's manifest marks {@code redactServerConf}. For those, absent or unrecognized
 *       means YBA can't tell whether the tar holds an unredacted server.conf, so the tar stays on
 *       the node.
 * </ul>
 *
 * <p><b>{@code EMPTY} may still carry a tar.</b> Most entrypoints exit before creating one, but
 * {@code collect_cdc_state} tars its metadata even at zero rows, so support can tell "queried, and
 * the table was empty" apart from "never collected".
 *
 * <p><b>server.conf is redacted only on the node</b>, by every component marked {@code
 * redactServerConf}, with the helper YBA deploys. A copy the node can't redact is deleted there and
 * reported as a {@code REDACTION_FAILURE=} line, which is logged here at ERROR, so an unredacted
 * file never crosses the wire. {@code REDACTION_STATUS=NODE} is trusted without re-checking, which
 * is why the helper must be a verified build ({@code UnverifiedNodeCopy.REJECT}).
 *
 * <p><b>A file the node fails to copy</b> is left out and reported as a {@code COPY_FAILURE=} line,
 * logged here at WARN; the rest of the component is still collected.
 *
 * <p><b>{@code bundlePath} is the whole node directory</b> ({@code bundleRoot/nodeName}, created by
 * {@code SupportBundleSubTaskBuilder}), shared by every spec and every component collected from
 * that node rather than scoped to one spec.
 */
@Slf4j
abstract class AbstractScriptTarNodeComponent implements SupportBundleComponent {

  private static final String BUNDLE_STATUS_KEY = "BUNDLE_STATUS";
  private static final String BUNDLE_PATH_KEY = "BUNDLE_PATH";
  private static final String REDACTION_STATUS_KEY = "REDACTION_STATUS";
  private static final List<String> SCRIPT_MARKER_KEYS =
      List.of(BUNDLE_STATUS_KEY, BUNDLE_PATH_KEY, REDACTION_STATUS_KEY);
  private static final long REMOTE_TAR_REMOVE_TIMEOUT_SECS = 60L;
  static final String COPY_FAILURE = "COPY_FAILURE";

  private final NodeUniverseManager nodeUniverseManager;
  private final SupportBundleRunAsSupport runAsSupport;
  private final SupportBundleCgroupScriptExecutor cgroupScriptExecutor;
  private final SupportBundleLogParseSupport logParseSupport;
  private final SupportBundleCdcStateDumpSupport cdcStateDumpSupport;
  private final SupportBundleServerConfRedactionSupport serverConfRedactionSupport;
  protected final SupportBundleScriptEstimateSupport scriptEstimateSupport;
  private final Config config;
  private final RuntimeConfGetter confGetter;
  private final String componentTypeLabel;

  AbstractScriptTarNodeComponent(
      String componentTypeLabel,
      NodeUniverseManager nodeUniverseManager,
      SupportBundleRunAsSupport runAsSupport,
      SupportBundleCgroupScriptExecutor cgroupScriptExecutor,
      SupportBundleLogParseSupport logParseSupport,
      SupportBundleCdcStateDumpSupport cdcStateDumpSupport,
      SupportBundleServerConfRedactionSupport serverConfRedactionSupport,
      SupportBundleScriptEstimateSupport scriptEstimateSupport,
      Config config,
      RuntimeConfGetter confGetter) {
    this.componentTypeLabel = componentTypeLabel;
    this.nodeUniverseManager = nodeUniverseManager;
    this.runAsSupport = runAsSupport;
    this.cgroupScriptExecutor = cgroupScriptExecutor;
    this.logParseSupport = logParseSupport;
    this.cdcStateDumpSupport = cdcStateDumpSupport;
    this.serverConfRedactionSupport = serverConfRedactionSupport;
    this.scriptEstimateSupport = scriptEstimateSupport;
    this.config = config;
    this.confGetter = confGetter;
  }

  protected abstract List<? extends ScriptTarComponentSpec> getSpecsFromBundleDetails(
      BundleDetails bundleDetails);

  /**
   * Returns estimated bytes for one logical sub-component on the given node. Keys in the outer
   * {@link #getFilesListWithSizes} map come from {@link ScriptTarComponentSpec#getComponentName()}.
   */
  protected abstract long estimateSubComponentSize(
      ScriptTarComponentSpec spec,
      String componentName,
      SupportBundleFormData bundleData,
      Universe universe,
      NodeDetails node,
      Date startDate,
      Date endDate,
      UUID stagingId);

  protected abstract List<? extends ScriptTarComponentSpec> getSpecs(
      SupportBundleTaskParams supportBundleTaskParams);

  @Override
  public void downloadComponent(
      SupportBundleTaskParams supportBundleTaskParams,
      Customer customer,
      Universe universe,
      Path bundlePath,
      NodeDetails node)
      throws Exception {
    throw new RuntimeException(
        String.format(
            "downloadComponent() method not applicable for '%s' without start and end date, on"
                + " universe = '%s'",
            componentTypeLabel, universe.getName()));
  }

  @Override
  public void downloadComponentBetweenDates(
      SupportBundleTaskParams supportBundleTaskParams,
      Customer customer,
      Universe universe,
      Path bundlePath,
      Date startDate,
      Date endDate,
      NodeDetails node)
      throws Exception {
    List<? extends ScriptTarComponentSpec> specs = getSpecs(supportBundleTaskParams);
    if (specs == null || specs.isEmpty()) {
      log.warn(
          "No specs provided for {} on universe = '{}'. Skipping.",
          componentTypeLabel,
          universe.getName());
      return;
    }

    collectScriptSpecs(
        supportBundleTaskParams, universe, bundlePath, node, startDate, endDate, specs);
  }

  /**
   * Runs every spec of this component on one node. A spec that throws is logged and the loop
   * continues: specs are independent sub-components, so one unreadable directory must not cost the
   * operator the rest of the bundle. {@code collectSpec} throws rather than logging, so that
   * decision is taken here and only here.
   */
  protected void collectScriptSpecs(
      SupportBundleTaskParams supportBundleTaskParams,
      Universe universe,
      Path bundlePath,
      NodeDetails node,
      Date startDate,
      Date endDate,
      List<? extends ScriptTarComponentSpec> specs)
      throws Exception {
    SupportBundleFormData bundleData = supportBundleTaskParams.bundleData;
    String nodeName = node.getNodeName();
    UUID bundleUuid =
        supportBundleTaskParams.supportBundle != null
            ? supportBundleTaskParams.supportBundle.getBundleUUID()
            : null;
    for (ScriptTarComponentSpec spec : specs) {
      SupportBundleCancellation.throwIfInterrupted();
      if (!SupportBundleClusterScope.componentRunsOn(universe, node, spec.getComponentName())) {
        log.info(
            "Skipping {} spec '{}' on node '{}': it needs the tserver process, which this node does"
                + " not run.",
            componentTypeLabel,
            spec.getComponentName(),
            nodeName);
        continue;
      }
      try {
        collectSpec(bundleUuid, spec, universe, bundlePath, node, bundleData, startDate, endDate);
      } catch (Exception e) {
        SupportBundleCancellation.rethrowIfCancelled(
            e,
            String.format(
                "%s spec '%s' on node '%s'",
                componentTypeLabel, spec.getComponentName(), nodeName));
        log.error(
            "Error collecting {} spec '{}' on node '{}': {}",
            componentTypeLabel,
            spec.getComponentName(),
            nodeName,
            e.getMessage(),
            e);
      }
    }
  }

  private void collectSpec(
      UUID bundleUuid,
      ScriptTarComponentSpec spec,
      Universe universe,
      Path bundlePath,
      NodeDetails node,
      SupportBundleFormData bundleData,
      Date startDate,
      Date endDate)
      throws Exception {
    String nodeName = node.getNodeName();
    // Never blank: the API rejects a spec that doesn't name a manifest member.
    String componentLabel = spec.getComponentName();

    String remoteTarPath =
        SupportBundleSpecTokenSupport.substituteSpecTokens(
            spec.getRemoteTarPath(), nodeName, bundleUuid);
    List<String> params =
        SupportBundleSpecTokenSupport.substituteSpecTokens(spec.getParams(), nodeName, bundleUuid);
    params =
        SupportBundleScriptParamResolver.resolveParams(
            params,
            componentLabel,
            universe,
            node,
            nodeUniverseManager,
            bundleData,
            startDate,
            endDate,
            config,
            confGetter);
    long timeoutSecs =
        spec.getTimeoutSecs() > 0
            ? spec.getTimeoutSecs()
            : SupportBundleV2ManifestSpecBuilder.DEFAULT_TIMEOUT_SECS;
    SupportBundleRunAsSupport.SpecContexts contexts =
        runAsSupport.contextsFor(spec, componentLabel, universe, node, timeoutSecs);
    ShellProcessContext context = contexts.run();
    ShellProcessContext deployContext = contexts.setup();

    // In the bundle's directory under the node's tmp directory, so a tar an abort or a failed rm
    // leaves behind goes with the bundle's cleanup. Another user (SystemLogs) can't write there.
    if (bundleUuid != null
        && StringUtils.isNotBlank(remoteTarPath)
        && !SupportBundleCgroupScriptExecutor.runsAsAnotherUser(context)) {
      remoteTarPath =
          cgroupScriptExecutor.stagingDir(bundleUuid, universe, node, deployContext)
              + "/"
              + Paths.get(remoteTarPath).getFileName();
      params = SupportBundleScriptParamResolver.withTarPath(params, remoteTarPath);
    }

    logParseSupport.ensureLogParseHelperOnNode(bundleUuid, node, universe, deployContext);

    // server.conf is redacted only on the node. Without a verified helper there, the node drops
    // it rather than send it unredacted.
    boolean redactServerConf =
        serverConfRedactionSupport.isServerConfRedactComponent(componentLabel);
    if (redactServerConf) {
      if (serverConfRedactionSupport.ensureRedactionBinaryOnNode(
          bundleUuid, node, universe, deployContext)) {
        params =
            serverConfRedactionSupport.appendRedactionScriptParams(
                bundleUuid, params, universe, node);
      } else {
        log.error(
            "{}: {} unavailable on node '{}' for component '{}'; its server.conf will be dropped"
                + " there",
            SupportBundleServerConfRedactionSupport.REDACTION_FAILURE,
            SupportBundleServerConfRedactionSupport.REDACT_BINARY_NAME,
            nodeName,
            componentLabel);
        params = serverConfRedactionSupport.appendDropServerConfScriptParam(params);
      }
    }

    if (SupportBundleCdcStateDumpSupport.COLLECT_CDC_STATE_COMPONENT.equals(componentLabel)) {
      if (!cdcStateDumpSupport.ensureDumpBinaryOnNode(bundleUuid, node, universe, deployContext)) {
        throw new RuntimeException(
            String.format(
                "collect_cdc_state on node '%s': failed to upload %s to %s",
                nodeName,
                SupportBundleCdcStateDumpSupport.BINARY_NAME,
                cdcStateDumpSupport.remoteBinaryPath(node, universe)));
      }
    }

    // From here on a tar may exist, even a partial one from a failed or cut-short run.
    try {
      ShellResponse response =
          executeScript(bundleUuid, spec, universe, node, params, context, deployContext);
      Map<String, String> markers = extractScriptMarkers(response.getMessage());
      String redactionStatus = markers.get(REDACTION_STATUS_KEY);
      boolean redactionOutcomeMissing =
          redactServerConf
              && !SupportBundleServerConfRedactionSupport.isKnownRedactionStatus(redactionStatus);
      logRedactionFailures(response.getMessage(), componentLabel, nodeName);
      logCopyFailures(response.getMessage(), componentLabel, "node '" + nodeName + "'");
      if (response.getCode() != 0) {
        if (redactionOutcomeMissing) {
          logRedactionOutcomeMissing(componentLabel, nodeName, "exit code " + response.getCode());
        }
        throw new RuntimeException(
            String.format(
                "Script execution failed on node '%s' for component '%s' with exit code %d. Output:"
                    + " %s",
                nodeName, componentLabel, response.getCode(), response.getMessage()));
      }

      String bundleStatus = markers.getOrDefault(BUNDLE_STATUS_KEY, "FAILED");
      String reportedTarPath = markers.get(BUNDLE_PATH_KEY);
      if (StringUtils.isNotBlank(reportedTarPath) && !reportedTarPath.equals(remoteTarPath)) {
        log.warn(
            "Ignoring BUNDLE_PATH '{}' from {} spec '{}' on node '{}'; using '{}'",
            reportedTarPath,
            componentTypeLabel,
            componentLabel,
            nodeName,
            remoteTarPath);
      }
      String generatedTarPath = remoteTarPath;
      if (redactionOutcomeMissing) {
        // Unknown whether the tar holds an unredacted server.conf, so it stays on the node.
        logRedactionOutcomeMissing(componentLabel, nodeName, "BUNDLE_STATUS=" + bundleStatus);
        if (!"FAILED".equals(bundleStatus)) {
          return;
        }
      }

      if ("EMPTY".equals(bundleStatus)) {
        // EMPTY usually means no tar was produced, but collect_cdc_state tars its metadata even at
        // zero rows, so probe before concluding there is nothing to copy.
        boolean emptyTarExists =
            StringUtils.isNotBlank(generatedTarPath)
                && SupportBundleNodeStaging.fileExists(
                    nodeUniverseManager, node, universe, generatedTarPath, context);
        if (!emptyTarExists) {
          log.warn(
              "{} spec '{}' found no matching files on node '{}' (stdout tail: {}).",
              componentTypeLabel,
              componentLabel,
              nodeName,
              truncateForLog(response.getMessage(), 500));
          return;
        }
        log.info(
            "{} spec '{}' returned EMPTY on node '{}'; including metadata tar '{}'.",
            componentTypeLabel,
            componentLabel,
            nodeName,
            generatedTarPath);
      } else if (!"OK".equals(bundleStatus)) {
        throw new RuntimeException(
            String.format(
                "Failed to generate tar on node '%s' for component '%s'. status=%s, stdout=%s",
                nodeName, componentLabel, bundleStatus, response.getMessage()));
      }

      if (StringUtils.isBlank(generatedTarPath)
          || !SupportBundleNodeStaging.fileExists(
              nodeUniverseManager, node, universe, generatedTarPath, context)) {
        log.warn(
            "Remote tar '{}' does not exist on node '{}' after {} spec '{}'; skipping copy."
                + " stdout={}",
            generatedTarPath,
            nodeName,
            componentTypeLabel,
            componentLabel,
            truncateForLog(response.getMessage(), 500));
        return;
      }

      copyAndExtractRemoteTar(
          bundleUuid,
          node,
          universe,
          context,
          generatedTarPath,
          bundlePath,
          componentLabel,
          nodeName);
    } finally {
      removeRemoteTar(node, universe, context, remoteTarPath);
    }
  }

  /**
   * Runs as the script's user, who owns the tar (in sticky /tmp for SystemLogs), with a short
   * timeout.
   */
  private void removeRemoteTar(
      NodeDetails node, Universe universe, ShellProcessContext context, String remoteTarPath) {
    if (StringUtils.isBlank(remoteTarPath)) {
      return;
    }
    ShellResponse[] response = new ShellResponse[1];
    try {
      SupportBundleCancellation.runIgnoringInterrupt(
          () ->
              response[0] =
                  nodeUniverseManager.runCommand(
                      node,
                      universe,
                      Arrays.asList("rm", "-f", remoteTarPath),
                      context.toBuilder().timeoutSecs(REMOTE_TAR_REMOVE_TIMEOUT_SECS).build(),
                      false));
    } catch (Exception e) {
      log.warn(
          "Could not remove support bundle tar '{}' on node '{}': {}",
          remoteTarPath,
          node.getNodeName(),
          e.getMessage());
      return;
    }
    if (response[0] != null && !response[0].isSuccess()) {
      log.warn(
          "Could not remove support bundle tar '{}' on node '{}': {}",
          remoteTarPath,
          node.getNodeName(),
          response[0].getMessage());
    }
  }

  /**
   * Copies the remote tar back and untars it into {@code bundlePath}.
   *
   * <p>The archive and its gunzipped form are staged outside the bundle; {@link
   * SupportBundleStaging} says why.
   */
  private void copyAndExtractRemoteTar(
      UUID bundleUuid,
      NodeDetails node,
      Universe universe,
      ShellProcessContext context,
      String generatedTarPath,
      Path bundlePath,
      String componentLabel,
      String nodeName)
      throws Exception {
    Path stagingDir =
        SupportBundleStaging.createDir(bundleUuid, nodeName + "-" + componentLabel + "-");
    Path localTar = stagingDir.resolve(componentLabel + ".tar.gz");
    Path downloaded = downloadTarget(node, universe, context, localTar);
    try {
      nodeUniverseManager.copyFileFromNode(
          node, universe, generatedTarPath, downloaded.toAbsolutePath().toString(), context);
      if (!Files.exists(downloaded)) {
        log.warn(
            "Remote tar '{}' was reported for component '{}' on node '{}', but local copy"
                + " failed.",
            generatedTarPath,
            componentLabel,
            nodeName);
        return;
      }
      File unZippedFile =
          com.yugabyte.yw.common.utils.FileUtils.unGzip(downloaded.toFile(), stagingDir.toFile());
      Files.delete(downloaded);
      com.yugabyte.yw.common.utils.FileUtils.unTarWithinDir(unZippedFile, bundlePath.toFile());
    } finally {
      // The staging directory goes below; a copy in java.io.tmpdir doesn't.
      Files.deleteIfExists(downloaded);
      if (!com.yugabyte.yw.common.utils.FileUtils.deleteDirectory(stagingDir.toFile())) {
        log.warn(
            "Failed to delete staging directory '{}' for component '{}' on node '{}'",
            stagingDir,
            componentLabel,
            nodeName);
      }
    }
  }

  /**
   * Without node agent, run_node_action.py's copy_file runs mkdir -p on the local parent on the
   * node, which can make /tmp but not YBA's storage path; so the tar lands in java.io.tmpdir first.
   */
  private Path downloadTarget(
      NodeDetails node, Universe universe, ShellProcessContext context, Path localTar) {
    if (!context.isUseSshConnectionOnly()
        && nodeUniverseManager.maybeUpgradeAndGetNodeAgent(universe, node).isPresent()) {
      return localTar;
    }
    return Paths.get(
        System.getProperty("java.io.tmpdir"),
        "support-bundle-" + UUID.randomUUID() + "-" + localTar.getFileName());
  }

  private ShellResponse executeScript(
      UUID bundleUuid,
      ScriptTarComponentSpec spec,
      Universe universe,
      NodeDetails node,
      List<String> params,
      ShellProcessContext context,
      ShellProcessContext stageContext) {
    Path scriptPath = resolveScriptPath(spec.getScriptPath());
    String componentLabel = spec.getComponentName();
    return cgroupScriptExecutor.executeScript(
        bundleUuid,
        universe,
        node,
        spec.getScriptPath(),
        scriptPath,
        params,
        context,
        stageContext,
        componentLabel,
        true /* applyLimits */);
  }

  private Path resolveScriptPath(String scriptPath) {
    Path path = Paths.get(scriptPath);
    if (!path.isAbsolute()) {
      path = Paths.get(config.getString("yb.devops.home"), scriptPath);
    }
    return path;
  }

  /**
   * Logs at ERROR each server.conf the node dropped because it could not redact it. Grepping YBA's
   * log for REDACTION_FAILURE accounts for a missing server.conf.
   */
  private static void logRedactionFailures(String output, String componentLabel, String nodeName) {
    if (StringUtils.isBlank(output)) {
      return;
    }
    String prefix = SupportBundleServerConfRedactionSupport.REDACTION_FAILURE + "=";
    for (String line : output.split("\\R")) {
      int start = line.indexOf(prefix);
      if (start >= 0) {
        log.error(
            "{} on node '{}' for component '{}': {}",
            SupportBundleServerConfRedactionSupport.REDACTION_FAILURE,
            nodeName,
            componentLabel,
            line.substring(start + prefix.length()).trim());
      }
    }
  }

  /** Logs at WARN each file the script left out because it could not copy it. */
  static void logCopyFailures(String output, String componentLabel, String where) {
    if (StringUtils.isBlank(output)) {
      return;
    }
    String prefix = COPY_FAILURE + "=";
    for (String line : output.split("\\R")) {
      String trimmed = line.trim();
      if (trimmed.startsWith(prefix)) {
        log.warn(
            "{} on {} for component '{}': {}",
            COPY_FAILURE,
            where,
            componentLabel,
            trimmed.substring(prefix.length()).trim());
      }
    }
  }

  private static void logRedactionOutcomeMissing(
      String componentLabel, String nodeName, String detail) {
    log.error(
        "{}: component '{}' on node '{}' reported no redaction outcome ({}); its server.conf is not"
            + " collected",
        SupportBundleServerConfRedactionSupport.REDACTION_FAILURE,
        componentLabel,
        nodeName,
        detail);
  }

  /**
   * Collects the {@code KEY=value} markers the bundle scripts print on stdout. Later occurrences of
   * a key win, matching the previous single-value parsing behavior.
   */
  private static Map<String, String> extractScriptMarkers(String output) {
    Map<String, String> markers = new HashMap<>();
    if (StringUtils.isBlank(output)) {
      return markers;
    }
    for (String line : output.split("\\R")) {
      String trimmed = line.trim();
      for (String key : SCRIPT_MARKER_KEYS) {
        String prefix = key + "=";
        if (trimmed.startsWith(prefix)) {
          markers.put(key, trimmed.substring(prefix.length()).trim());
          break;
        }
      }
    }
    return markers;
  }

  private static String truncateForLog(String message, int maxLen) {
    if (message == null) {
      return "";
    }
    String trimmed = message.trim();
    if (trimmed.length() <= maxLen) {
      return trimmed;
    }
    return trimmed.substring(0, maxLen) + "...";
  }

  @Override
  public Map<String, Long> getFilesListWithSizes(
      Customer customer,
      SupportBundleFormData bundleData,
      Universe universe,
      Date startDate,
      Date endDate,
      NodeDetails node)
      throws Exception {
    return getFilesListWithSizes(customer, bundleData, null, universe, startDate, endDate, node);
  }

  @Override
  public Map<String, Long> getFilesListWithSizes(
      Customer customer,
      SupportBundleFormData bundleData,
      BundleDetails bundleDetails,
      Universe universe,
      Date startDate,
      Date endDate,
      NodeDetails node)
      throws Exception {
    Map<String, Long> result = new HashMap<>();
    if (bundleData == null || node == null) {
      return result;
    }
    List<? extends ScriptTarComponentSpec> specs = getSpecsFromBundleDetails(bundleDetails);
    if (specs == null || specs.isEmpty()) {
      return result;
    }
    // Estimates have no bundle, so this node's specs share a staging id of their own.
    UUID stagingId = UUID.randomUUID();
    try {
      for (ScriptTarComponentSpec spec : specs) {
        SupportBundleCancellation.throwIfInterrupted();
        String componentName = spec.getComponentName();
        // Keep the estimate in step with what collection will actually do on this node.
        if (!SupportBundleClusterScope.componentRunsOn(universe, node, componentName)) {
          continue;
        }
        result.put(
            componentName,
            estimateSubComponentSize(
                spec, componentName, bundleData, universe, node, startDate, endDate, stagingId));
      }
    } finally {
      SupportBundleCancellation.runIgnoringInterrupt(
          () -> scriptEstimateSupport.cleanupStaging(stagingId, universe));
    }
    return result;
  }
}
