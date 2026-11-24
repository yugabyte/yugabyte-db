// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundlev2;

import static com.yugabyte.yw.common.supportbundlev2.ScriptParamsFlags.ensureFlagValue;
import static com.yugabyte.yw.common.supportbundlev2.ScriptParamsFlags.replaceFlagValue;

import com.typesafe.config.Config;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.forms.SupportBundleFormData;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;

/**
 * Adjusts v2 script params at execution time so node-level paths match legacy v1 components
 * (provider {@code YB_HOME_DIR}, k8s gflag overrides, collection dates, etc.).
 *
 * <p>This is the second and last of two substitution stages. {@link SupportBundleSpecTokenSupport}
 * expands the {@code ${...}} tokens a manifest author writes; this class supplies everything the
 * manifest cannot know statically -- values that depend on the universe, the node, the provider,
 * runtime config, or the requested date window. Keeping it here rather than in the manifest is what
 * lets one spec serve k8s and VM universes, and lets v2 land on the same paths v1 used, which is
 * the property that makes the two comparable.
 *
 * <p>Each component type has its own resolver ({@link FilesComponentParamsResolver}, {@link
 * BashComponentParamsResolver}, {@link YbaComponentParamsResolver}); YSQL, YCQL and yb-admin
 * members need none, since their args are built into the spec. A member with no resolver of its own
 * keeps its params, apart from the directory flags the Files resolver fills in.
 */
public final class SupportBundleScriptParamResolver {

  private static final String REMOTE_TAR_PATH_FLAG = "--remote_tar_path";
  private static final String LOCAL_TAR_PATH_FLAG = "--local_tar_path";

  private SupportBundleScriptParamResolver() {}

  /**
   * The YBA-host variant. There is no node, so only the date window and the ApplicationLogs paths
   * can be resolved.
   */
  static List<String> resolveYbaParams(
      List<String> params,
      String componentName,
      Universe universe,
      SupportBundleFormData bundleData,
      Date startDate,
      Date endDate,
      Config config,
      RuntimeConfGetter confGetter) {
    if (params == null || params.isEmpty()) {
      return List.of();
    }
    return YbaComponentParamsResolver.resolve(
        params, componentName, startDate, endDate, config, confGetter);
  }

  static List<String> resolveParams(
      List<String> params,
      String componentName,
      Universe universe,
      NodeDetails node,
      NodeUniverseManager nodeUniverseManager,
      SupportBundleFormData bundleData,
      Date startDate,
      Date endDate,
      Config config,
      RuntimeConfGetter confGetter) {
    if (params == null || params.isEmpty()) {
      return List.of();
    }
    if (BashComponentParamsResolver.handles(componentName)) {
      return BashComponentParamsResolver.resolve(
          params, componentName, universe, node, nodeUniverseManager, config);
    }
    return FilesComponentParamsResolver.resolve(
        params,
        componentName,
        universe,
        node,
        nodeUniverseManager,
        bundleData,
        startDate,
        endDate,
        config,
        confGetter);
  }

  /** Points the tar path flags at {@code tarPath}, adding {@code --remote_tar_path} if none. */
  public static List<String> withTarPath(List<String> params, String tarPath) {
    List<String> result = params == null ? new ArrayList<>() : new ArrayList<>(params);
    if (result.contains(LOCAL_TAR_PATH_FLAG)) {
      result = replaceFlagValue(result, LOCAL_TAR_PATH_FLAG, tarPath);
      if (!result.contains(REMOTE_TAR_PATH_FLAG)) {
        return result;
      }
    }
    return ensureFlagValue(result, REMOTE_TAR_PATH_FLAG, tarPath);
  }
}
