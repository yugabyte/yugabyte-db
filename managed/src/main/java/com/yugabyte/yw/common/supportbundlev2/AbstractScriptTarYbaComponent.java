// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundlev2;

import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.tasks.params.SupportBundleTaskParams;
import com.yugabyte.yw.common.ShellProcessContext;
import com.yugabyte.yw.common.ShellProcessHandler;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.supportbundle.SupportBundleComponent;
import com.yugabyte.yw.common.supportbundlev2.manifest.SupportBundleV2ManifestSpecBuilder;
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
import java.util.ArrayList;
import java.util.Date;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import org.apache.commons.lang3.tuple.Pair;

/**
 * Base class for global-level YBA host components that run a local bash script, read the produced
 * tar from stdout markers, and untar the result into the YBA bundle directory.
 *
 * <p>The YBA-host counterpart of {@link AbstractScriptTarNodeComponent}, and deliberately the same
 * shape so a manifest spec reads the same whichever side runs it. Two differences follow from there
 * being no remote node: the script runs through {@link ShellProcessHandler} instead of SSH, and the
 * tar is already local, so it is untarred in place rather than copied back.
 *
 * <p>Only {@code BUNDLE_STATUS} and {@code BUNDLE_PATH} apply here. There is no {@code
 * REDACTION_STATUS}: these components stage no {@code server.conf}, and nothing would be gained by
 * redacting on the host that is about to read the file anyway.
 */
@Slf4j
abstract class AbstractScriptTarYbaComponent implements SupportBundleComponent {

  private static final String BUNDLE_STATUS_PREFIX = "BUNDLE_STATUS=";
  private static final String BUNDLE_PATH_PREFIX = "BUNDLE_PATH=";

  private final ShellProcessHandler shellProcessHandler;
  private final SupportBundleScriptEstimateSupport scriptEstimateSupport;
  private final Config config;
  private final RuntimeConfGetter confGetter;
  private final String componentTypeLabel;

  AbstractScriptTarYbaComponent(
      String componentTypeLabel,
      ShellProcessHandler shellProcessHandler,
      SupportBundleScriptEstimateSupport scriptEstimateSupport,
      Config config,
      RuntimeConfGetter confGetter) {
    this.componentTypeLabel = componentTypeLabel;
    this.shellProcessHandler = shellProcessHandler;
    this.scriptEstimateSupport = scriptEstimateSupport;
    this.config = config;
    this.confGetter = confGetter;
  }

  protected abstract List<? extends ScriptTarComponentSpec> getSpecs(
      SupportBundleTaskParams supportBundleTaskParams);

  protected abstract List<? extends ScriptTarComponentSpec> getSpecsFromBundleDetails(
      BundleDetails bundleDetails);

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
                + " %s",
            componentTypeLabel, universeLabel(universe)));
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
          "No specs provided for {} on {}. Skipping.", componentTypeLabel, universeLabel(universe));
      return;
    }

    SupportBundleFormData bundleData = supportBundleTaskParams.bundleData;
    UUID bundleUuid =
        supportBundleTaskParams.supportBundle != null
            ? supportBundleTaskParams.supportBundle.getBundleUUID()
            : null;
    for (ScriptTarComponentSpec spec : specs) {
      SupportBundleCancellation.throwIfInterrupted();
      try {
        collectSpec(spec, universe, bundlePath, bundleData, startDate, endDate, bundleUuid);
      } catch (Exception e) {
        SupportBundleCancellation.rethrowIfCancelled(
            e,
            String.format("%s spec '%s' on YBA host", componentTypeLabel, spec.getComponentName()));
        log.error(
            "Error collecting {} spec '{}' on YBA host",
            componentTypeLabel,
            spec.getComponentName(),
            e);
      }
    }
  }

  /**
   * Runs one spec and folds its tar into the bundle. Throws on failure; the caller decides whether
   * one bad spec should cost the rest, matching the node-side split.
   */
  private void collectSpec(
      ScriptTarComponentSpec spec,
      Universe universe,
      Path bundlePath,
      SupportBundleFormData bundleData,
      Date startDate,
      Date endDate,
      UUID bundleUuid)
      throws Exception {
    String componentLabel = spec.getComponentName();

    // There is no node here, so only ${bundleUuid} can expand -- but it must, or the literal token
    // reaches the exec as part of a path. Concurrent bundles otherwise share one tar path on the
    // YBA host, and unlike the node components that path is shared across universes too.
    String localTarPath =
        SupportBundleSpecTokenSupport.substituteSpecTokens(
            spec.getRemoteTarPath(), null, bundleUuid);
    List<String> params =
        new ArrayList<>(
            SupportBundleSpecTokenSupport.substituteSpecTokens(spec.getParams(), null, bundleUuid));
    params =
        SupportBundleScriptParamResolver.resolveYbaParams(
            params, componentLabel, universe, bundleData, startDate, endDate, config, confGetter);

    log.info(
        "Collecting {} spec '{}' on YBA host with params: {}",
        componentTypeLabel,
        componentLabel,
        params);

    long timeoutSecs =
        spec.getTimeoutSecs() > 0
            ? spec.getTimeoutSecs()
            : SupportBundleV2ManifestSpecBuilder.DEFAULT_TIMEOUT_SECS;
    // The script's TMPDIR, deleted here: a timed-out script is killed with SIGKILL, so its own
    // trap can't clean up.
    Path workDir = SupportBundleStaging.createDir(bundleUuid, componentLabel + "-work-");
    ShellProcessContext context =
        ShellProcessContext.builder()
            .logCmdOutput(true)
            .timeoutSecs(timeoutSecs)
            .extraEnvVars(Map.of("TMPDIR", workDir.toString()))
            .build();

    // From here on a tar may exist, even a partial one from a failed or cut-short run.
    try {
      ShellResponse response =
          SupportBundleCancellation.throwIfCancelled(executeScript(spec, params, context));
      AbstractScriptTarNodeComponent.logCopyFailures(
          response.getMessage(), componentLabel, "YBA host");
      if (response.getCode() != 0) {
        throw new RuntimeException(
            String.format(
                "Script execution failed on YBA host for component '%s' with exit code %d. Output:"
                    + " %s",
                componentLabel, response.getCode(), response.getMessage()));
      }

      Pair<String, String> statusAndPath = extractBundleStatusAndPath(response.getMessage());
      String bundleStatus = statusAndPath.getLeft();
      String reportedTarPath = statusAndPath.getRight();
      if (StringUtils.isNotBlank(reportedTarPath) && !reportedTarPath.equals(localTarPath)) {
        log.warn(
            "Ignoring BUNDLE_PATH '{}' from {} spec '{}'; using '{}'",
            reportedTarPath,
            componentTypeLabel,
            componentLabel,
            localTarPath);
      }
      // YBA copies and deletes only the path it asked for, never one the script reports.
      String generatedTarPath = localTarPath;

      if ("EMPTY".equals(bundleStatus)) {
        log.warn(
            "{} spec '{}' found no matching files on YBA host. script stdout: {}",
            componentTypeLabel,
            componentLabel,
            response.getMessage());
        return;
      }
      if (!"OK".equals(bundleStatus)) {
        throw new RuntimeException(
            String.format(
                "Failed to generate tar on YBA host for component '%s'. status=%s, stdout=%s",
                componentLabel, bundleStatus, response.getMessage()));
      }

      if (StringUtils.isBlank(generatedTarPath) || !Files.exists(Paths.get(generatedTarPath))) {
        log.warn(
            "Local tar '{}' does not exist for component '{}'; skipping untar. script stdout: {}",
            generatedTarPath,
            componentLabel,
            response.getMessage());
        return;
      }

      extractTar(bundleUuid, Paths.get(generatedTarPath), bundlePath, componentLabel);
    } finally {
      if (StringUtils.isNotBlank(localTarPath)) {
        try {
          Files.deleteIfExists(Paths.get(localTarPath));
        } catch (Exception e) {
          log.warn("Could not remove support bundle tar '{}': {}", localTarPath, e.getMessage());
        }
      }
      if (!com.yugabyte.yw.common.utils.FileUtils.deleteDirectory(workDir.toFile())) {
        log.warn("Failed to delete '{}' for component '{}'", workDir, componentLabel);
      }
    }
  }

  /**
   * Gunzips straight from the script's tar into staging, then untars into the bundle: no copy of a
   * possibly multi-GB archive, and no archive is ever written inside the bundle.
   */
  private void extractTar(UUID bundleUuid, Path tarPath, Path bundlePath, String componentLabel)
      throws Exception {
    Path stagingDir = SupportBundleStaging.createDir(bundleUuid, "YBA-" + componentLabel + "-");
    try {
      File unZippedFile =
          com.yugabyte.yw.common.utils.FileUtils.unGzip(tarPath.toFile(), stagingDir.toFile());
      com.yugabyte.yw.common.utils.FileUtils.unTarWithinDir(unZippedFile, bundlePath.toFile());
    } finally {
      if (!com.yugabyte.yw.common.utils.FileUtils.deleteDirectory(stagingDir.toFile())) {
        log.warn(
            "Failed to delete staging directory '{}' for component '{}' on YBA host",
            stagingDir,
            componentLabel);
      }
    }
  }

  private ShellResponse executeScript(
      ScriptTarComponentSpec spec, List<String> params, ShellProcessContext context) {
    Path scriptPath = resolveScriptPath(spec.getScriptPath());
    if (!Files.isRegularFile(scriptPath)) {
      throw new RuntimeException(
          String.format(
              "YBA support bundle script not found at '%s' (resolved from '%s').",
              scriptPath.toAbsolutePath(), spec.getScriptPath()));
    }
    List<String> command = new ArrayList<>();
    command.add("/bin/bash");
    command.add(scriptPath.toAbsolutePath().toString());
    command.addAll(params);
    return shellProcessHandler.run(command, context);
  }

  private Path resolveScriptPath(String scriptPath) {
    Path path = Paths.get(scriptPath);
    if (!path.isAbsolute()) {
      path = Paths.get(config.getString("yb.devops.home"), scriptPath);
    }
    return path;
  }

  /** These components also run for YBA-only bundles, which have no universe to name. */
  private static String universeLabel(Universe universe) {
    return universe == null ? "YBA platform host" : "universe = '" + universe.getName() + "'";
  }

  /**
   * Reads the two markers the script prints on stdout. Later occurrences win, and an absent status
   * is read as {@code FAILED} so a script that dies before printing anything is never mistaken for
   * a successful empty collection. See {@link AbstractScriptTarNodeComponent} for the full marker
   * protocol these scripts share.
   */
  private Pair<String, String> extractBundleStatusAndPath(String output) {
    String status = null;
    String bundlePath = null;
    if (StringUtils.isNotBlank(output)) {
      String[] lines = output.split("\\R");
      for (String line : lines) {
        String trimmed = line.trim();
        if (trimmed.startsWith(BUNDLE_STATUS_PREFIX)) {
          status = trimmed.substring(BUNDLE_STATUS_PREFIX.length()).trim();
        } else if (trimmed.startsWith(BUNDLE_PATH_PREFIX)) {
          bundlePath = trimmed.substring(BUNDLE_PATH_PREFIX.length()).trim();
        }
      }
    }
    if (status == null) {
      status = "FAILED";
    }
    return Pair.of(status, bundlePath);
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
    if (bundleData == null) {
      return result;
    }
    List<? extends ScriptTarComponentSpec> specs = getSpecsFromBundleDetails(bundleDetails);
    if (specs == null || specs.isEmpty()) {
      return result;
    }
    for (ScriptTarComponentSpec spec : specs) {
      SupportBundleCancellation.throwIfInterrupted();
      String componentName = spec.getComponentName();
      long size;
      if (SupportBundleV2EstimateRegistry.APPLICATION_LOGS_COMPONENT.equals(componentName)) {
        size =
            scriptEstimateSupport.runYbaScriptEstimate(
                spec, componentName, universe, bundleData, startDate, endDate);
      } else {
        size = SupportBundleV2EstimateRegistry.PLACEHOLDER_10KB;
      }
      result.put(componentName, size);
    }
    return result;
  }
}
