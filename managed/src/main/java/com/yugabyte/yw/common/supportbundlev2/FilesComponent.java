// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundlev2;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.tasks.params.SupportBundleTaskParams;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.supportbundlev2.cgroup.SupportBundleCgroupScriptExecutor;
import com.yugabyte.yw.common.supportbundlev2.nodebin.SupportBundleCdcStateDumpSupport;
import com.yugabyte.yw.common.supportbundlev2.nodebin.SupportBundleLogParseSupport;
import com.yugabyte.yw.common.supportbundlev2.nodebin.SupportBundleServerConfRedactionSupport;
import com.yugabyte.yw.forms.SupportBundleFormData;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.BundleDetails;
import com.yugabyte.yw.models.helpers.BundleDetails.FilesComponentSpec;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.ScriptTarComponentSpec;
import java.util.Date;
import java.util.List;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;

/**
 * Generic, spec-driven node-level support bundle component. For each {@link FilesComponentSpec} it
 * runs the spec's script on the node through {@link NodeUniverseManager#runCommand}, which uses
 * node agent where it can and SSH otherwise. The script only creates a tar on the node; this
 * component then retrieves the tar via the legacy {@link NodeUniverseManager#copyFileFromNode} flow
 * and untars it into the per-node bundle directory.
 */
@Slf4j
@Singleton
public class FilesComponent extends AbstractScriptTarNodeComponent {

  @Inject
  FilesComponent(
      NodeUniverseManager nodeUniverseManager,
      SupportBundleRunAsSupport runAsSupport,
      SupportBundleCgroupScriptExecutor cgroupScriptExecutor,
      SupportBundleLogParseSupport logParseSupport,
      SupportBundleCdcStateDumpSupport cdcStateDumpSupport,
      SupportBundleServerConfRedactionSupport serverConfRedactionSupport,
      SupportBundleScriptEstimateSupport scriptEstimateSupport,
      Config config,
      RuntimeConfGetter confGetter) {
    super(
        "FilesComponent",
        nodeUniverseManager,
        runAsSupport,
        cgroupScriptExecutor,
        logParseSupport,
        cdcStateDumpSupport,
        serverConfRedactionSupport,
        scriptEstimateSupport,
        config,
        confGetter);
  }

  @Override
  protected List<? extends ScriptTarComponentSpec> getSpecs(
      SupportBundleTaskParams supportBundleTaskParams) {
    if (supportBundleTaskParams.supportBundle == null
        || supportBundleTaskParams.supportBundle.getBundleDetails() == null) {
      return null;
    }
    return supportBundleTaskParams.supportBundle.getBundleDetails().getFilesComponentSpecs();
  }

  @Override
  protected List<? extends ScriptTarComponentSpec> getSpecsFromBundleDetails(
      BundleDetails bundleDetails) {
    return bundleDetails == null ? null : bundleDetails.getFilesComponentSpecs();
  }

  @Override
  protected long estimateSubComponentSize(
      ScriptTarComponentSpec spec,
      String componentName,
      SupportBundleFormData bundleData,
      Universe universe,
      NodeDetails node,
      Date startDate,
      Date endDate,
      UUID stagingId) {
    if (SupportBundleV2EstimateRegistry.isPlaceholder10KbFileComponent(componentName)) {
      return SupportBundleV2EstimateRegistry.PLACEHOLDER_10KB;
    }
    if (SupportBundleV2EstimateRegistry.isBashEstimateNodeFileComponent(componentName)) {
      return scriptEstimateSupport.runNodeScriptEstimate(
          spec, componentName, bundleData, universe, node, startDate, endDate, stagingId);
    }
    log.warn(
        "Unknown FilesComponent spec '{}'; using {} byte placeholder",
        componentName,
        SupportBundleV2EstimateRegistry.PLACEHOLDER_10KB);
    return SupportBundleV2EstimateRegistry.PLACEHOLDER_10KB;
  }
}
