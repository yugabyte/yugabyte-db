// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundlev2;

import com.yugabyte.yw.models.helpers.BundleDetails.ComponentType;
import java.util.Optional;
import java.util.Set;
import java.util.stream.Collectors;
import java.util.stream.Stream;

/**
 * Decides how each file component is sized, and maps collection entrypoints to their estimate
 * twins. Other component types size themselves.
 *
 * <ul>
 *   <li><b>Placeholder</b> -- a fixed constant. These components are a handful of small files whose
 *       size is not worth a remote round trip per node; the estimate exists so the component still
 *       appears in the breakdown.
 *   <li><b>Bash estimate</b> -- run the collection script's measure-only twin on the node and read
 *       the size off stdout. Used where the answer genuinely varies by orders of magnitude (logs,
 *       cores, metadata).
 * </ul>
 *
 * <p>The sets are disjoint; a component in neither falls through to the caller's own default.
 */
public final class SupportBundleV2EstimateRegistry {

  public static final long PLACEHOLDER_10KB = 10_000L;
  public static final long PLACEHOLDER_5KB = 5_000L;
  public static final String YBA_NODE_KEY = "YBA";
  public static final String APPLICATION_LOGS_COMPONENT = ComponentType.ApplicationLogs.name();

  private static final Set<String> PLACEHOLDER_10KB_FILE_COMPONENTS =
      names(
          ComponentType.OutputFiles,
          ComponentType.ErrorFiles,
          ComponentType.GFlags,
          ComponentType.Instance);

  private static final Set<String> BASH_ESTIMATE_NODE_FILE_COMPONENTS =
      names(
          ComponentType.UniverseLogs,
          ComponentType.SystemLogs,
          ComponentType.YbcLogs,
          ComponentType.CoreFiles,
          ComponentType.ConsensusMeta,
          ComponentType.TabletMeta,
          ComponentType.NodeAgent);

  private SupportBundleV2EstimateRegistry() {}

  private static Set<String> names(ComponentType... types) {
    return Stream.of(types).map(ComponentType::name).collect(Collectors.toUnmodifiableSet());
  }

  public static boolean isPlaceholder10KbFileComponent(String componentName) {
    return PLACEHOLDER_10KB_FILE_COMPONENTS.contains(componentName);
  }

  public static boolean isBashEstimateNodeFileComponent(String componentName) {
    return BASH_ESTIMATE_NODE_FILE_COMPONENTS.contains(componentName);
  }

  /**
   * Derives the estimate entrypoint from the collection one by the {@code create_<x>_bundle} ->
   * {@code estimate_<x>_size} naming convention that {@code support_bundle_utils.sh} follows, so a
   * manifest can add a collection entrypoint and get its estimate counterpart for free.
   *
   * <p>An entrypoint outside that convention -- the {@code collect_*} ones, for instance -- returns
   * empty rather than a guessed name, and the caller derives from the component name instead.
   * Guessing here would mean invoking a shell function that does not exist on the node.
   */
  public static Optional<String> toEstimateEntrypoint(String createEntrypoint) {
    if (createEntrypoint == null || createEntrypoint.isBlank()) {
      return Optional.empty();
    }
    if (createEntrypoint.startsWith("create_") && createEntrypoint.endsWith("_bundle")) {
      String core =
          createEntrypoint.substring(
              "create_".length(), createEntrypoint.length() - "_bundle".length());
      return Optional.of("estimate_" + core + "_size");
    }
    return Optional.empty();
  }

  /**
   * Used when a component has no create entrypoint to derive from, so the CamelCase component name
   * is snake_cased instead. The second regex keeps acronym runs together, so {@code YbcLogs}
   * becomes {@code ybc_logs} and {@code K8sInfo} does not split mid-acronym.
   */
  public static String toEstimateEntrypointFromComponentName(String componentName) {
    return "estimate_"
        + componentName
            .replaceAll("([a-z])([A-Z])", "$1_$2")
            .replaceAll("([A-Z]+)([A-Z][a-z])", "$1_$2")
            .toLowerCase()
        + "_size";
  }
}
