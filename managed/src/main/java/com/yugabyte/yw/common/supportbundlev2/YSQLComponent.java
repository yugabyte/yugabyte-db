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
import com.yugabyte.yw.models.helpers.BundleDetails.YSQLComponentSpec;
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

/** Runs YSQL queries on each node via ysqlsh and stores combined output in the support bundle. */
@Slf4j
@Singleton
public class YSQLComponent implements SupportBundleComponent {

  private final SupportBundleCgroupCommandExecutor cgroupCommandExecutor;
  private final SupportBundleClusterScope clusterScope;

  @Inject
  YSQLComponent(
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
            "downloadComponent() method not applicable for 'YSQLComponent' without start and end"
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
    List<YSQLComponentSpec> specs = getSpecs(supportBundleTaskParams);
    if (specs == null || specs.isEmpty()) {
      log.warn(
          "No ysqlComponentSpecs provided for YSQLComponent on universe = '{}'. Skipping.",
          universe.getName());
      return;
    }

    // ysqlsh talks to the local YSQL endpoint, which only exists alongside a tserver.
    if (!SupportBundleClusterScope.componentRunsOn(node, ComponentType.YSQLComponent)) {
      log.info(
          "Skipping YSQLComponent on node '{}': the node does not run a tserver.",
          node.getNodeName());
      return;
    }

    UUID bundleUuid =
        supportBundleTaskParams.supportBundle != null
            ? supportBundleTaskParams.supportBundle.getBundleUUID()
            : null;
    for (YSQLComponentSpec spec : specs) {
      if (!clusterScope.runsOn(
          bundleUuid, ComponentType.YSQLComponent, spec.getComponentName(), node)) {
        continue;
      }
      try {
        collectSpec(bundleUuid, spec, universe, bundlePath, node);
      } catch (Exception e) {
        SupportBundleCancellation.rethrowIfCancelled(
            e,
            String.format(
                "YSQLComponent spec '%s' on node '%s'",
                spec.getComponentName(), node.getNodeName()));
        log.error(
            "Error collecting YSQLComponent spec '{}' on node '{}'",
            spec.getComponentName(),
            node.getNodeName(),
            e);
      }
    }
  }

  private void collectSpec(
      UUID bundleUuid, YSQLComponentSpec spec, Universe universe, Path bundlePath, NodeDetails node)
      throws Exception {
    String componentLabel = spec.getComponentName();
    if (spec.getQueries() == null || spec.getQueries().isEmpty()) {
      log.warn(
          "Skipping YSQLComponent spec '{}' on node '{}': queries is empty.",
          componentLabel,
          node.getNodeName());
      return;
    }

    String dbName = StringUtils.defaultIfBlank(spec.getDbName(), "yugabyte");
    long timeoutSecs =
        spec.getTimeoutSecs() > 0
            ? spec.getTimeoutSecs()
            : SupportBundleV2ManifestSpecBuilder.DEFAULT_QUERY_TIMEOUT_SECS;
    boolean authEnabled =
        universe.getUniverseDetails().getPrimaryCluster().userIntent.isYSQLAuthEnabled();
    boolean cpEnabled =
        universe.getUniverseDetails().getPrimaryCluster().userIntent.enableConnectionPooling;

    StringBuilder output = new StringBuilder();
    output.append("node=").append(node.getNodeName()).append('\n');
    output.append("db=").append(dbName).append('\n');
    output.append('\n');

    ShellResponse response =
        cgroupCommandExecutor.runYsqlQueries(
            bundleUuid,
            node,
            universe,
            dbName,
            spec.getQueries(),
            timeoutSecs,
            authEnabled,
            cpEnabled,
            componentLabel);
    output.append(response.getMessage()).append('\n');

    String outputFileName =
        StringUtils.defaultIfBlank(spec.getOutputFileName(), componentLabel + ".txt");
    Path outputPath = bundlePath.resolve(outputFileName);
    Files.write(outputPath, output.toString().getBytes(StandardCharsets.UTF_8));
  }

  private List<YSQLComponentSpec> getSpecs(SupportBundleTaskParams supportBundleTaskParams) {
    if (supportBundleTaskParams.supportBundle == null
        || supportBundleTaskParams.supportBundle.getBundleDetails() == null) {
      return null;
    }
    return supportBundleTaskParams.supportBundle.getBundleDetails().getYsqlComponentSpecs();
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
    if (bundleDetails == null || bundleDetails.getYsqlComponentSpecs() == null) {
      return result;
    }
    for (YSQLComponentSpec spec : bundleDetails.getYsqlComponentSpecs()) {
      result.put(spec.getComponentName(), SupportBundleV2EstimateRegistry.PLACEHOLDER_5KB);
    }
    return result;
  }
}
