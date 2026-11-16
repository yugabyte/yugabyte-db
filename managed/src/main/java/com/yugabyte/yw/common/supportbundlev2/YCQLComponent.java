// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundlev2;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.commissioner.tasks.params.SupportBundleTaskParams;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.supportbundle.SupportBundleComponent;
import com.yugabyte.yw.common.supportbundlev2.cgroup.SupportBundleCgroupCommandExecutor;
import com.yugabyte.yw.common.supportbundlev2.manifest.SupportBundleV2ManifestSpecBuilder;
import com.yugabyte.yw.forms.SupportBundleFormData;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.BundleDetails;
import com.yugabyte.yw.models.helpers.BundleDetails.ComponentType;
import com.yugabyte.yw.models.helpers.BundleDetails.YCQLComponentSpec;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Date;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

/**
 * Runs YCQL queries on each node via ycqlsh and stores combined output in the support bundle. With
 * YCQL auth on it records a skip instead: YBA has no current YCQL password, only the create-time
 * one, which password changes never update.
 */
@Slf4j
@Singleton
public class YCQLComponent implements SupportBundleComponent {

  static final String AUTH_SKIP_REASON =
      "YCQL authentication is enabled and YBA has no YCQL credentials";

  private final SupportBundleCgroupCommandExecutor cgroupCommandExecutor;
  private final SupportBundleClusterScope clusterScope;

  @Inject
  YCQLComponent(
      SupportBundleCgroupCommandExecutor cgroupCommandExecutor,
      SupportBundleClusterScope clusterScope) {
    this.cgroupCommandExecutor = cgroupCommandExecutor;
    this.clusterScope = clusterScope;
  }

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
            "downloadComponent() method not applicable for 'YCQLComponent' without start and end"
                + " date, on universe = '%s'",
            universe.getName()));
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
    List<YCQLComponentSpec> specs = getSpecs(supportBundleTaskParams);
    if (specs == null || specs.isEmpty()) {
      log.warn(
          "No ycqlComponentSpecs provided for YCQLComponent on universe = '{}'. Skipping.",
          universe.getName());
      return;
    }

    // ycqlsh talks to the local YCQL endpoint, which only exists alongside a tserver.
    if (!SupportBundleClusterScope.componentRunsOn(node, ComponentType.YCQLComponent)) {
      log.info(
          "Skipping YCQLComponent on node '{}': the node does not run a tserver.",
          node.getNodeName());
      return;
    }

    UUID bundleUuid =
        supportBundleTaskParams.supportBundle != null
            ? supportBundleTaskParams.supportBundle.getBundleUUID()
            : null;
    for (YCQLComponentSpec spec : specs) {
      if (!clusterScope.runsOn(
          bundleUuid, ComponentType.YCQLComponent, spec.getComponentName(), node)) {
        continue;
      }
      try {
        collectSpec(bundleUuid, spec, universe, bundlePath, node);
      } catch (Exception e) {
        SupportBundleCancellation.rethrowIfCancelled(
            e,
            String.format(
                "YCQLComponent spec '%s' on node '%s'",
                spec.getComponentName(), node.getNodeName()));
        log.error(
            "Error collecting YCQLComponent spec '{}' on node '{}'",
            spec.getComponentName(),
            node.getNodeName(),
            e);
      }
    }
  }

  private void collectSpec(
      UUID bundleUuid, YCQLComponentSpec spec, Universe universe, Path bundlePath, NodeDetails node)
      throws Exception {
    String componentLabel = spec.getComponentName();
    if (spec.getQueries() == null || spec.getQueries().isEmpty()) {
      log.warn(
          "Skipping YCQLComponent spec '{}' on node '{}': queries is empty.",
          componentLabel,
          node.getNodeName());
      return;
    }

    long timeoutSecs =
        spec.getTimeoutSecs() > 0
            ? spec.getTimeoutSecs()
            : SupportBundleV2ManifestSpecBuilder.DEFAULT_QUERY_TIMEOUT_SECS;

    StringBuilder output = new StringBuilder();
    output.append("node=").append(node.getNodeName()).append('\n');
    if (StringUtils.isNotBlank(spec.getKeyspace())) {
      output.append("keyspace=").append(spec.getKeyspace()).append('\n');
    }
    output.append('\n');

    if (skipsForAuth(universe)) {
      log.info(
          "Skipping YCQLComponent spec '{}' on node '{}': {}",
          componentLabel,
          node.getNodeName(),
          AUTH_SKIP_REASON);
      output.append("skipped: ").append(AUTH_SKIP_REASON).append('\n');
    } else {
      ShellResponse response =
          cgroupCommandExecutor.runYcqlQueries(
              bundleUuid,
              node,
              universe,
              spec.getKeyspace(),
              spec.getQueries(),
              timeoutSecs,
              componentLabel);
      output.append(response.getMessage()).append('\n');
    }

    String outputFileName =
        StringUtils.defaultIfBlank(spec.getOutputFileName(), componentLabel + ".txt");
    Path outputPath = bundlePath.resolve(outputFileName);
    Files.write(outputPath, output.toString().getBytes(StandardCharsets.UTF_8));
  }

  private List<YCQLComponentSpec> getSpecs(SupportBundleTaskParams supportBundleTaskParams) {
    if (supportBundleTaskParams.supportBundle == null
        || supportBundleTaskParams.supportBundle.getBundleDetails() == null) {
      return null;
    }
    return supportBundleTaskParams.supportBundle.getBundleDetails().getYcqlComponentSpecs();
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
    if (bundleDetails == null || bundleDetails.getYcqlComponentSpecs() == null) {
      return result;
    }
    // With auth on, collection writes only a one-line skip note.
    long size = skipsForAuth(universe) ? 0L : SupportBundleV2EstimateRegistry.PLACEHOLDER_5KB;
    for (YCQLComponentSpec spec : bundleDetails.getYcqlComponentSpecs()) {
      result.put(spec.getComponentName(), size);
    }
    return result;
  }

  static boolean skipsForAuth(Universe universe) {
    return universe.getUniverseDetails().getPrimaryCluster().userIntent.enableYCQLAuth;
  }
}
