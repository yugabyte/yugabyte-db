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
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.ScriptTarComponentSpec;
import java.util.Date;
import java.util.List;
import java.util.UUID;

/**
 * Spec-driven node-level component for bash/script diagnostics (e.g. top/ps/pmap stats). Uses the
 * same script-and-remote-tar flow as {@link FilesComponent}.
 */
@Singleton
public class BashComponent extends AbstractScriptTarNodeComponent {

  @Inject
  BashComponent(
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
        "BashComponent",
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
    return supportBundleTaskParams.supportBundle.getBundleDetails().getBashComponentSpecs();
  }

  @Override
  protected List<? extends ScriptTarComponentSpec> getSpecsFromBundleDetails(
      BundleDetails bundleDetails) {
    return bundleDetails == null ? null : bundleDetails.getBashComponentSpecs();
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
    return SupportBundleV2EstimateRegistry.PLACEHOLDER_5KB;
  }
}
