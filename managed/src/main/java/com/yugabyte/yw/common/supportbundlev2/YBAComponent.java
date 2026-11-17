// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundlev2;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.tasks.params.SupportBundleTaskParams;
import com.yugabyte.yw.common.ShellProcessHandler;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.helpers.BundleDetails;
import com.yugabyte.yw.models.helpers.ScriptTarComponentSpec;
import java.util.List;

/**
 * Spec-driven global-level component for collecting artifacts from the YBA platform host via local
 * bash scripts (e.g. ApplicationLogs). Uses the same script-and-tar contract as {@link
 * FilesComponent}, but executes locally instead of on DB nodes.
 */
@Singleton
public class YBAComponent extends AbstractScriptTarYbaComponent {

  @Inject
  YBAComponent(
      ShellProcessHandler shellProcessHandler,
      SupportBundleScriptEstimateSupport scriptEstimateSupport,
      Config config,
      RuntimeConfGetter confGetter) {
    super("YBAComponent", shellProcessHandler, scriptEstimateSupport, config, confGetter);
  }

  @Override
  protected List<? extends ScriptTarComponentSpec> getSpecs(
      SupportBundleTaskParams supportBundleTaskParams) {
    if (supportBundleTaskParams.supportBundle == null
        || supportBundleTaskParams.supportBundle.getBundleDetails() == null) {
      return null;
    }
    return supportBundleTaskParams.supportBundle.getBundleDetails().getYbaComponentSpecs();
  }

  @Override
  protected List<? extends ScriptTarComponentSpec> getSpecsFromBundleDetails(
      BundleDetails bundleDetails) {
    return bundleDetails == null ? null : bundleDetails.getYbaComponentSpecs();
  }
}
