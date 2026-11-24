// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundlev2;

import static com.yugabyte.yw.common.supportbundlev2.ScriptParamsFlags.ensureFlagValue;
import static com.yugabyte.yw.common.supportbundlev2.ScriptParamsFlags.formatDay;
import static com.yugabyte.yw.common.supportbundlev2.ScriptParamsFlags.formatUniverseLogsInstant;
import static com.yugabyte.yw.common.supportbundlev2.ScriptParamsFlags.getOverriddenGflagValue;
import static com.yugabyte.yw.common.supportbundlev2.ScriptParamsFlags.indexOfFlag;
import static com.yugabyte.yw.common.supportbundlev2.ScriptParamsFlags.replaceFlagValue;
import static com.yugabyte.yw.common.supportbundlev2.ScriptParamsFlags.truncateToDay;

import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.KubernetesTaskBase;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.common.supportbundle.SupportBundleK8sSupport;
import com.yugabyte.yw.forms.SupportBundleFormData;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.BundleDetails;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Base64;
import java.util.Date;
import java.util.List;
import java.util.Optional;
import java.util.Set;
import org.apache.commons.lang3.StringUtils;

/** Resolves the per-node params of FilesComponent members: the log collectors and file lists. */
final class FilesComponentParamsResolver {

  private static final Set<String> YB_HOME_DIR_COMPONENTS =
      Set.of("GFlags", "OutputFiles", "ErrorFiles");

  // Dir flags for components that tar a list of paths relative to a base directory. Which flag the
  // CLI sends declares which directory the component needs.
  private static final String NODE_HOME_DIR_FLAG = "--node_home_dir";
  private static final String NODE_DATA_DIR_FLAG = "--node_data_dir";
  private static final String NODE_AGENT_HOME_DIR_FLAG = "--node_agent_home_dir";

  private static final String CORE_FILES_COMPONENT = "CoreFiles";
  private static final String SYSTEM_LOGS_COMPONENT = "SystemLogs";
  private static final String UNIVERSE_LOGS_COMPONENT = "UniverseLogs";
  private static final String YBC_LOGS_COMPONENT = "YbcLogs";
  private static final String YBC_CONF_RELATIVE_PATH = "controller/conf/server.conf";
  private static final String YBC_LOGS_REGEX_CONFIG_KEY =
      "yb.support_bundle.ybc_logs_regex_pattern";
  private static final String UNIVERSE_LOGS_REGEX_CONFIG_KEY =
      "yb.support_bundle.universe_logs_regex_pattern";
  private static final String POSTGRES_LOGS_REGEX_CONFIG_KEY =
      "yb.support_bundle.postgres_logs_regex_pattern";
  private static final String CONNECTION_POOLING_LOGS_REGEX_CONFIG_KEY =
      "yb.support_bundle.connection_pooling_logs_regex_pattern";
  private static final long MILLIS_PER_SECOND = 1000L;
  private static final String SYSTEM_LOGS_REGEX_CONFIG_KEY =
      "yb.support_bundle.system_logs_regex_pattern";
  private static final String COLLECT_JOURNALD_LOGS_CONFIG_KEY =
      "yb.support_bundle.collect_journald_logs";
  private static final String NODE_AGENT_INSTALL_PATH_CONFIG_KEY =
      "yb.node_agent.server.install_path";
  private static final String LOG_DIR_GFLAG = "log_dir";
  private static final String YNP_LOG_FILE = "app.log";

  private FilesComponentParamsResolver() {}

  /** A member with no resolver of its own only gets the directory flags it declares. */
  static List<String> resolve(
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
    if (UNIVERSE_LOGS_COMPONENT.equals(componentName)) {
      return resolveUniverseLogsParams(
          params, universe, node, bundleData, startDate, endDate, config, confGetter);
    }
    if (CORE_FILES_COMPONENT.equals(componentName)) {
      return resolveCoreFilesParams(
          params, universe, node, nodeUniverseManager, bundleData, startDate, endDate);
    }
    if (SYSTEM_LOGS_COMPONENT.equals(componentName)) {
      return resolveSystemLogsParams(params, universe, startDate, endDate, config, confGetter);
    }
    if (YBC_LOGS_COMPONENT.equals(componentName)) {
      return resolveYbcLogsParams(
          params, universe, node, nodeUniverseManager, config, startDate, endDate);
    }
    return resolveFileListDirParams(
        params, componentName, universe, node, nodeUniverseManager, config, confGetter);
  }

  /**
   * Resolves the base directory for components that tar a fixed list of paths relative to it. The
   * directory value is owned by the server rather than the manifest, since it varies per universe
   * and node.
   *
   * <p>{@code --node_data_dir} and {@code --node_agent_home_dir} need no component list: the flag
   * itself declares which directory the component wants, so a new component of either kind requires
   * no change here. {@code --node_home_dir} still consults {@link #YB_HOME_DIR_COMPONENTS} because
   * that flag is overloaded elsewhere in this class.
   */
  private static List<String> resolveFileListDirParams(
      List<String> params,
      String componentName,
      Universe universe,
      NodeDetails node,
      NodeUniverseManager nodeUniverseManager,
      Config config,
      RuntimeConfGetter confGetter) {
    if (indexOfFlag(params, NODE_DATA_DIR_FLAG) >= 0) {
      if (config == null) {
        return params;
      }
      String dataDir = Util.getDataDirectoryPath(universe, node, config) + "/yb-data";
      return replaceFlagValue(params, NODE_DATA_DIR_FLAG, dataDir);
    }
    if (indexOfFlag(params, NODE_AGENT_HOME_DIR_FLAG) >= 0) {
      String nodeAgentHome = resolveNodeAgentHome(node, config, confGetter);
      return StringUtils.isBlank(nodeAgentHome)
          ? params
          : replaceFlagValue(params, NODE_AGENT_HOME_DIR_FLAG, nodeAgentHome);
    }
    if (!YB_HOME_DIR_COMPONENTS.contains(componentName)) {
      return params;
    }
    String ybHomeDir = resolveYbHomeDir(componentName, universe, node, nodeUniverseManager);
    return replaceFlagValue(params, NODE_HOME_DIR_FLAG, ybHomeDir);
  }

  /**
   * Mirrors {@code NodeAgentComponent}: the home a node agent recorded at install time is
   * authoritative, since the install path is configurable per deployment. Falls back to the
   * configured install path for nodes that have no node agent record, and returns null when neither
   * is available so the caller leaves the flag untouched.
   */
  private static String resolveNodeAgentHome(
      NodeDetails node, Config config, RuntimeConfGetter confGetter) {
    if (node != null
        && node.cloudInfo != null
        && StringUtils.isNotBlank(node.cloudInfo.private_ip)) {
      Optional<NodeAgent> nodeAgent = NodeAgent.maybeGetByIp(node.cloudInfo.private_ip);
      if (nodeAgent.isPresent() && StringUtils.isNotBlank(nodeAgent.get().getHome())) {
        return nodeAgent.get().getHome();
      }
    }
    String installPath = null;
    if (confGetter != null) {
      installPath = confGetter.getGlobalConf(GlobalConfKeys.nodeAgentInstallPath);
    } else if (config != null && config.hasPath(NODE_AGENT_INSTALL_PATH_CONFIG_KEY)) {
      installPath = config.getString(NODE_AGENT_INSTALL_PATH_CONFIG_KEY);
    }
    return StringUtils.isBlank(installPath) ? null : installPath + "/node-agent";
  }

  private static List<String> resolveCoreFilesParams(
      List<String> params,
      Universe universe,
      NodeDetails node,
      NodeUniverseManager nodeUniverseManager,
      SupportBundleFormData bundleData,
      Date startDate,
      Date endDate) {
    String ybHomeDir = nodeUniverseManager.getYbHomeDir(node, universe);
    List<String> resolved = replaceFlagValue(params, "--node_home_dir", ybHomeDir);
    resolved = replaceFlagValue(resolved, "--cores_dir", resolveCoresDir(universe, ybHomeDir));

    if (startDate != null) {
      resolved = replaceFlagValue(resolved, "--start_date", formatDay(truncateToDay(startDate)));
    }
    if (endDate != null) {
      resolved = replaceFlagValue(resolved, "--end_date", formatDay(endDate));
    }
    if (bundleData != null) {
      if (bundleData.maxNumRecentCores > 0) {
        resolved =
            replaceFlagValue(
                resolved, "--max_num_recent_cores", String.valueOf(bundleData.maxNumRecentCores));
      }
      if (bundleData.maxCoreFileSize > 0) {
        resolved =
            replaceFlagValue(
                resolved, "--max_core_file_size", String.valueOf(bundleData.maxCoreFileSize));
      }
    }
    return resolved;
  }

  /**
   * Cores are written under the mounted volume on kubernetes rather than the yb home, which is what
   * {@code GFlagsUtil} points the ybc {@code cores_dir} at for those universes.
   */
  private static String resolveCoresDir(Universe universe, String ybHomeDir) {
    if (SupportBundleK8sSupport.isK8s(universe)) {
      return GFlagsUtil.K8S_YBC_CORES_DIR;
    }
    return ybHomeDir + GFlagsUtil.CORES_DIR_PATH;
  }

  private static List<String> resolveUniverseLogsParams(
      List<String> params,
      Universe universe,
      NodeDetails node,
      SupportBundleFormData bundleData,
      Date startDate,
      Date endDate,
      Config config,
      RuntimeConfGetter confGetter) {
    List<String> resolved = params;

    if (config != null) {
      String mountPath = Util.getDataDirectoryPath(universe, node, config);
      String nodeHomeDir = mountPath + "/yb-data";
      String masterLogsPath = nodeHomeDir + "/master/logs";
      String tserverLogsPath = nodeHomeDir + "/tserver/logs";

      String masterLogDirOverride =
          getOverriddenGflagValue(universe, UniverseTaskBase.ServerType.MASTER, LOG_DIR_GFLAG);
      if (masterLogDirOverride != null) {
        masterLogsPath = masterLogDirOverride;
      }
      String tserverLogDirOverride =
          getOverriddenGflagValue(universe, UniverseTaskBase.ServerType.TSERVER, LOG_DIR_GFLAG);
      if (tserverLogDirOverride != null) {
        tserverLogsPath = tserverLogDirOverride;
      }

      String ynpLogPath = resolveYnpLogPath(config, confGetter);

      resolved = ensureFlagValue(resolved, "--mount_path", mountPath);
      resolved = ensureFlagValue(resolved, "--node_home_dir", nodeHomeDir);
      resolved = ensureFlagValue(resolved, "--master_logs_path", masterLogsPath);
      resolved = ensureFlagValue(resolved, "--tserver_logs_path", tserverLogsPath);
      if (ynpLogPath != null) {
        resolved = ensureFlagValue(resolved, "--ynp_log_path", ynpLogPath);
      }
    }

    if (startDate != null) {
      resolved = replaceFlagValue(resolved, "--start_date", formatUniverseLogsInstant(startDate));
    }
    if (endDate != null) {
      resolved = replaceFlagValue(resolved, "--end_date", formatUniverseLogsInstant(endDate));
    }
    // v1 UniverseLogsComponent reads filterPgAuditLogs from bundleData; v2 FilesComponent specs
    // embed --filter_pgaudit_logs in params (e.g. supportBundleV2.yaml) and must not be
    // overwritten.
    if (bundleData != null && usesLegacyUniverseLogsComponent(bundleData)) {
      resolved =
          replaceFlagValue(
              resolved, "--filter_pgaudit_logs", String.valueOf(bundleData.filterPgAuditLogs));
    }

    resolved = stripRegexFlags(resolved);
    for (String pattern : resolveUniverseLogsRegexPatterns(universe, config, confGetter)) {
      if (StringUtils.isNotBlank(pattern)) {
        resolved.add("--regex-b64");
        resolved.add(encodeRegexForScript(pattern));
      }
    }
    return resolved;
  }

  /**
   * Hands the SystemLogs script the same universe-scoped settings v1's SystemLogsComponent reads,
   * so an operator's runtime config means one thing across both bundle versions.
   *
   * <p>The selection regex travels base64 encoded under the existing {@code --regex-b64} flag: the
   * pattern is full of backslashes and parentheses, and the params are shell quoted on the cgroup
   * transport and passed as argv otherwise.
   *
   * <p>The window is also sent as epoch seconds. journalctl parses {@code --since} in the node's
   * local time zone, and the day-granular dates the other components use would silently widen a
   * journal dump to whole days.
   */
  private static List<String> resolveSystemLogsParams(
      List<String> params,
      Universe universe,
      Date startDate,
      Date endDate,
      Config config,
      RuntimeConfGetter confGetter) {
    List<String> resolved = params;
    if (startDate != null) {
      resolved = replaceFlagValue(resolved, "--start_date", formatUniverseLogsInstant(startDate));
      resolved =
          ensureFlagValue(
              resolved, "--start_epoch", String.valueOf(startDate.getTime() / MILLIS_PER_SECOND));
    }
    if (endDate != null) {
      resolved = replaceFlagValue(resolved, "--end_date", formatUniverseLogsInstant(endDate));
      resolved =
          ensureFlagValue(
              resolved, "--end_epoch", String.valueOf(endDate.getTime() / MILLIS_PER_SECOND));
    }

    resolved = stripRegexFlags(resolved);
    String pattern = resolveSystemLogsRegexPattern(universe, config, confGetter);
    if (StringUtils.isNotBlank(pattern)) {
      resolved.add("--regex-b64");
      resolved.add(encodeRegexForScript(pattern));
    }
    // ensureFlagValue rather than replaceFlagValueIfBlank: runtime config is authoritative, so a
    // value an older yba-cli put in the spec must not win.
    return ensureFlagValue(
        resolved,
        "--collect_journald",
        String.valueOf(resolveCollectJournaldLogs(universe, config, confGetter)));
  }

  private static String resolveSystemLogsRegexPattern(
      Universe universe, Config config, RuntimeConfGetter confGetter) {
    if (confGetter != null) {
      return confGetter.getConfForScope(universe, UniverseConfKeys.systemLogsRegexPattern);
    }
    if (config != null && config.hasPath(SYSTEM_LOGS_REGEX_CONFIG_KEY)) {
      return config.getString(SYSTEM_LOGS_REGEX_CONFIG_KEY);
    }
    // Omitted rather than blanked, so the script falls back to its own built-in default.
    return null;
  }

  private static boolean resolveCollectJournaldLogs(
      Universe universe, Config config, RuntimeConfGetter confGetter) {
    if (confGetter != null) {
      return confGetter.getConfForScope(universe, UniverseConfKeys.collectJournaldLogs);
    }
    if (config != null && config.hasPath(COLLECT_JOURNALD_LOGS_CONFIG_KEY)) {
      return config.getBoolean(COLLECT_JOURNALD_LOGS_CONFIG_KEY);
    }
    // Matches reference.conf. A validation path with no config must not quietly disable the
    // journal, which on a node with no rsyslog is the only OS evidence there is.
    return true;
  }

  private static String resolveYnpLogPath(Config config, RuntimeConfGetter confGetter) {
    String installPath = null;
    if (confGetter != null) {
      installPath = confGetter.getGlobalConf(GlobalConfKeys.nodeAgentInstallPath);
    } else if (config.hasPath(NODE_AGENT_INSTALL_PATH_CONFIG_KEY)) {
      installPath = config.getString(NODE_AGENT_INSTALL_PATH_CONFIG_KEY);
    }
    if (StringUtils.isBlank(installPath)) {
      return null;
    }
    return installPath + "/node-agent/logs/" + YNP_LOG_FILE;
  }

  private static List<String> resolveUniverseLogsRegexPatterns(
      Universe universe, Config config, RuntimeConfGetter confGetter) {
    if (confGetter != null) {
      return Arrays.asList(
          confGetter.getConfForScope(universe, UniverseConfKeys.universeLogsRegexPattern),
          confGetter.getConfForScope(universe, UniverseConfKeys.postgresLogsRegexPattern),
          confGetter.getConfForScope(universe, UniverseConfKeys.connectionPoolingLogsRegexPattern));
    }
    return Arrays.asList(
        config.getString(UNIVERSE_LOGS_REGEX_CONFIG_KEY),
        config.getString(POSTGRES_LOGS_REGEX_CONFIG_KEY),
        config.getString(CONNECTION_POOLING_LOGS_REGEX_CONFIG_KEY));
  }

  private static boolean usesLegacyUniverseLogsComponent(SupportBundleFormData bundleData) {
    return bundleData.components != null
        && bundleData.components.contains(BundleDetails.ComponentType.UniverseLogs);
  }

  private static List<String> stripRegexFlags(List<String> params) {
    List<String> result = new ArrayList<>(params.size());
    for (int i = 0; i < params.size(); i++) {
      if ("--regex".equals(params.get(i)) || "--regex-b64".equals(params.get(i))) {
        i++;
        continue;
      }
      result.add(params.get(i));
    }
    return result;
  }

  private static List<String> resolveYbcLogsParams(
      List<String> params,
      Universe universe,
      NodeDetails node,
      NodeUniverseManager nodeUniverseManager,
      Config config,
      Date startDate,
      Date endDate) {
    if (config == null) {
      return params;
    }
    String mountPath = Util.getDataDirectoryPath(universe, node, config);
    String nodeHomeDir = mountPath + "/ybc-data";
    String ybcBase = resolveYbcServerConfBase(universe, node, nodeUniverseManager);
    String ybcLogsPath = resolveYbcLogsPath(universe, nodeHomeDir);
    String ybcServerConfPath = ybcBase + "/" + YBC_CONF_RELATIVE_PATH;

    List<String> resolved = replaceFlagValue(params, "--node_home_dir", nodeHomeDir);
    resolved = ensureFlagValue(resolved, "--ybc_logs_path", ybcLogsPath);
    resolved = ensureFlagValue(resolved, "--ybc_server_conf_path", ybcServerConfPath);

    if (startDate != null) {
      resolved = replaceFlagValue(resolved, "--start_date", formatUniverseLogsInstant(startDate));
    }
    if (endDate != null) {
      resolved = replaceFlagValue(resolved, "--end_date", formatUniverseLogsInstant(endDate));
    }

    if (config.hasPath(YBC_LOGS_REGEX_CONFIG_KEY)) {
      String regexPattern = config.getString(YBC_LOGS_REGEX_CONFIG_KEY);
      if (StringUtils.isNotBlank(regexPattern)) {
        resolved = ensureFlagValue(resolved, "--regex-b64", encodeRegexForScript(regexPattern));
      }
    }
    return resolved;
  }

  private static String resolveYbcLogsPath(Universe universe, String nodeHomeDir) {
    CloudType cloudType = universe.getUniverseDetails().getPrimaryCluster().userIntent.providerType;
    if (CloudType.kubernetes.equals(cloudType)) {
      return GFlagsUtil.K8S_YBC_LOG_SUBDIR;
    }
    return nodeHomeDir + GFlagsUtil.YBC_LOG_SUBDIR;
  }

  private static String resolveYbcServerConfBase(
      Universe universe, NodeDetails node, NodeUniverseManager nodeUniverseManager) {
    CloudType cloudType = universe.getUniverseDetails().getPrimaryCluster().userIntent.providerType;
    if (CloudType.kubernetes.equals(cloudType)) {
      return KubernetesTaskBase.K8S_NODE_YW_DATA_DIR;
    }
    return nodeUniverseManager.getYbHomeDir(node, universe);
  }

  /**
   * Log-matching regexes are operator-configurable and full of characters a shell would eat, so
   * they travel base64-encoded and are decoded on the node rather than quoted along the way.
   */
  private static String encodeRegexForScript(String pattern) {
    return Base64.getEncoder().encodeToString(pattern.getBytes(StandardCharsets.UTF_8));
  }

  /**
   * On Kubernetes the GFlags component reads its files from the gflag-override home rather than the
   * usual yb home, which is why this is not simply {@code getYbHomeDir}. Every other component, and
   * every non-k8s universe, takes the normal path.
   */
  private static String resolveYbHomeDir(
      String componentName,
      Universe universe,
      NodeDetails node,
      NodeUniverseManager nodeUniverseManager) {
    if ("GFlags".equals(componentName)) {
      CloudType cloudType =
          universe.getUniverseDetails().getPrimaryCluster().userIntent.providerType;
      if (CloudType.kubernetes.equals(cloudType)) {
        return GFlagsUtil.K8S_HOME_DIR_FOR_GFLAG_OVERRIDES;
      }
    }
    return nodeUniverseManager.getYbHomeDir(node, universe);
  }
}
