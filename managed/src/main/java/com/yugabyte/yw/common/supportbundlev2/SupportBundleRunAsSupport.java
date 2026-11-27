// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundlev2;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.ImageBundleUtil;
import com.yugabyte.yw.common.ShellProcessContext;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.supportbundlev2.manifest.SupportBundleV2Manifest;
import com.yugabyte.yw.common.supportbundlev2.manifest.SupportBundleV2ManifestLoader;
import com.yugabyte.yw.common.supportbundlev2.nodebin.SupportBundleNodeBinarySupport;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.ScriptTarComponentSpec;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

/** Picks the user a node script runs as, for collect and estimate, from runAs in YBA's manifest. */
@Slf4j
@Singleton
public class SupportBundleRunAsSupport {

  private final ImageBundleUtil imageBundleUtil;
  private final SupportBundleV2ManifestLoader manifestLoader;

  @Inject
  public SupportBundleRunAsSupport(
      ImageBundleUtil imageBundleUtil, SupportBundleV2ManifestLoader manifestLoader) {
    this.imageBundleUtil = imageBundleUtil;
    this.manifestLoader = manifestLoader;
  }

  private boolean shouldRunAsProviderSshUser(String componentName) {
    SupportBundleV2Manifest.Component config =
        manifestLoader.load().getSpec().getComponentConfigs().get(componentName);
    return config != null && config.getRunAs() == SupportBundleV2Manifest.RunAs.PROVIDER_SSH_USER;
  }

  /** The context a spec's script runs under, and the one its setup on the node runs under. */
  public record SpecContexts(ShellProcessContext run, ShellProcessContext setup) {}

  /**
   * The script runs as runAs in YBA's manifest -- the provider ssh user for SystemLogs, which needs
   * sudo for /var/log, else the spec's user -- with the spec's timeout. Setup (the bundle's
   * directory, staged scripts, helpers, the tar move) always runs as the spec's user, yugabyte,
   * which owns them; see {@link SupportBundleNodeBinarySupport#installContext} for its timeout.
   */
  public SpecContexts contextsFor(
      ScriptTarComponentSpec spec,
      String componentName,
      Universe universe,
      NodeDetails node,
      long timeoutSecs) {
    ShellProcessContext.ShellProcessContextBuilder builder =
        ShellProcessContext.builder().logCmdOutput(true).timeoutSecs(timeoutSecs);
    applyRunUser(builder, spec, componentName, universe, node);
    ShellProcessContext run = builder.build();
    return new SpecContexts(
        run, SupportBundleNodeBinarySupport.installContext(run, spec.getLinuxUser()));
  }

  /** The provider ssh user if the manifest says so, else the spec's user (pinned to yugabyte). */
  private void applyRunUser(
      ShellProcessContext.ShellProcessContextBuilder contextBuilder,
      ScriptTarComponentSpec spec,
      String componentName,
      Universe universe,
      NodeDetails node) {
    if (shouldRunAsProviderSshUser(componentName)) {
      applyProviderSshUser(contextBuilder, universe, node);
    } else if (StringUtils.isNotBlank(spec.getLinuxUser())) {
      contextBuilder.sshUser(spec.getLinuxUser());
    }
  }

  /** Same resolution as the rest of YBA, including image-bundle and per-node overrides. */
  public void applyProviderSshUser(
      ShellProcessContext.ShellProcessContextBuilder contextBuilder,
      Universe universe,
      NodeDetails node) {
    try {
      Provider provider = Util.getProviderForNode(node, universe);
      String sshUser =
          imageBundleUtil.findEffectiveSshUser(
              provider, universe, universe.getCluster(node.placementUuid).userIntent, node);
      if (StringUtils.isNotBlank(node.sshUserOverride)) {
        sshUser = node.sshUserOverride;
      }
      if (StringUtils.isNotBlank(sshUser)) {
        contextBuilder.sshUser(sshUser).useDefaultUser(false);
      }
    } catch (Exception e) {
      log.debug("Unable to resolve provider SSH user for node '{}'", node.getNodeName(), e);
    }
  }
}
