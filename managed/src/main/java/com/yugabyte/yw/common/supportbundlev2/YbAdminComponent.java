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
import com.yugabyte.yw.models.helpers.BundleDetails.YbAdminComponentSpec;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Collections;
import java.util.Date;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

/** Runs yb-admin commands on each node and stores output in the support bundle. */
@Slf4j
@Singleton
public class YbAdminComponent implements SupportBundleComponent {

  private final SupportBundleCgroupCommandExecutor cgroupCommandExecutor;
  private final SupportBundleClusterScope clusterScope;

  @Inject
  YbAdminComponent(
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
            "downloadComponent() method not applicable for 'YbAdminComponent' without start and end"
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
    List<YbAdminComponentSpec> specs = getSpecs(supportBundleTaskParams);
    if (specs == null || specs.isEmpty()) {
      log.warn(
          "No ybAdminComponentSpecs provided for YbAdminComponent on universe = '{}'. Skipping.",
          universe.getName());
      return;
    }

    UUID bundleUuid =
        supportBundleTaskParams.supportBundle != null
            ? supportBundleTaskParams.supportBundle.getBundleUUID()
            : null;
    for (YbAdminComponentSpec spec : specs) {
      if (!clusterScope.runsOn(
          bundleUuid, ComponentType.YbAdminComponent, spec.getComponentName(), node)) {
        continue;
      }
      try {
        collectSpec(bundleUuid, spec, universe, bundlePath, node);
      } catch (Exception e) {
        SupportBundleCancellation.rethrowIfCancelled(
            e,
            String.format(
                "YbAdminComponent spec '%s' on node '%s'",
                spec.getComponentName(), node.getNodeName()));
        log.error(
            "Error collecting YbAdminComponent spec '{}' on node '{}'",
            spec.getComponentName(),
            node.getNodeName(),
            e);
      }
    }
  }

  private void collectSpec(
      UUID bundleUuid,
      YbAdminComponentSpec spec,
      Universe universe,
      Path bundlePath,
      NodeDetails node)
      throws Exception {
    String componentLabel = spec.getComponentName();
    List<String> commands = resolveCommands(spec);
    if (commands.isEmpty()) {
      log.warn(
          "Skipping YbAdminComponent spec '{}' on node '{}': ybAdminCommands is empty.",
          componentLabel,
          node.getNodeName());
      return;
    }

    long timeoutSecs =
        spec.getTimeoutSecs() > 0
            ? spec.getTimeoutSecs()
            : SupportBundleV2ManifestSpecBuilder.DEFAULT_QUERY_TIMEOUT_SECS;
    List<String> args =
        spec.getYbAdminArgs() == null ? Collections.emptyList() : spec.getYbAdminArgs();

    StringBuilder output = new StringBuilder();
    output.append("node=").append(node.getNodeName()).append('\n');
    output.append('\n');

    ShellResponse response =
        cgroupCommandExecutor.runYbAdminCommands(
            bundleUuid, node, universe, commands, args, timeoutSecs, componentLabel);
    output.append(response.getMessage()).append('\n');

    String outputFileName =
        StringUtils.defaultIfBlank(spec.getOutputFileName(), componentLabel + ".txt");
    Path outputPath = bundlePath.resolve(outputFileName);
    Files.write(outputPath, output.toString().getBytes(StandardCharsets.UTF_8));
  }

  private static List<String> resolveCommands(YbAdminComponentSpec spec) {
    return spec.getYbAdminCommands() == null ? Collections.emptyList() : spec.getYbAdminCommands();
  }

  private List<YbAdminComponentSpec> getSpecs(SupportBundleTaskParams supportBundleTaskParams) {
    if (supportBundleTaskParams.supportBundle == null
        || supportBundleTaskParams.supportBundle.getBundleDetails() == null) {
      return null;
    }
    return supportBundleTaskParams.supportBundle.getBundleDetails().getYbAdminComponentSpecs();
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
    if (bundleDetails == null || bundleDetails.getYbAdminComponentSpecs() == null) {
      return result;
    }
    for (YbAdminComponentSpec spec : bundleDetails.getYbAdminComponentSpecs()) {
      result.put(spec.getComponentName(), SupportBundleV2EstimateRegistry.PLACEHOLDER_5KB);
    }
    return result;
  }
}
