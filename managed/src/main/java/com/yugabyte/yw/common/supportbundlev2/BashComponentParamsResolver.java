// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundlev2;

import static com.yugabyte.yw.common.supportbundlev2.ScriptParamsFlags.ensureFlagValue;
import static com.yugabyte.yw.common.supportbundlev2.ScriptParamsFlags.getOverriddenGflagValue;

import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.NodeUniverseManager;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.common.supportbundle.SupportBundleK8sSupport;
import com.yugabyte.yw.common.supportbundlev2.cgroup.SupportBundleRemoteCommandBuilder;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.ArrayList;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;
import org.apache.commons.lang3.StringUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/** Resolves the per-node params of BashComponent members, the node diagnostics scripts. */
final class BashComponentParamsResolver {

  private static final Logger LOG = LoggerFactory.getLogger(BashComponentParamsResolver.class);

  private static final Set<String> BASH_COMPONENTS = Set.of("collect_top_ps_pmap_stats");
  private static final String COLLECT_DATA_DIR_MAP_COMPONENT = "collect_data_dir_map";
  private static final String COLLECT_THREADZ_MEMZ_RPCZ_STATS_COMPONENT =
      "collect_threadz_memz_rpcz_stats";
  private static final String COLLECT_HEAP_SNAPSHOT_COMPONENT = "collect_heap_snapshot";
  private static final String COLLECT_CDC_STATE_COMPONENT = "collect_cdc_state";
  private static final String LOCALHOST_IP = "127.0.0.1";

  private BashComponentParamsResolver() {}

  static boolean handles(String componentName) {
    return BASH_COMPONENTS.contains(componentName)
        || COLLECT_DATA_DIR_MAP_COMPONENT.equals(componentName)
        || COLLECT_THREADZ_MEMZ_RPCZ_STATS_COMPONENT.equals(componentName)
        || COLLECT_HEAP_SNAPSHOT_COMPONENT.equals(componentName)
        || COLLECT_CDC_STATE_COMPONENT.equals(componentName);
  }

  static List<String> resolve(
      List<String> params,
      String componentName,
      Universe universe,
      NodeDetails node,
      NodeUniverseManager nodeUniverseManager,
      Config config) {
    if (BASH_COMPONENTS.contains(componentName)) {
      return resolveBashComponentParams(params, universe, node, nodeUniverseManager, config);
    }
    if (COLLECT_DATA_DIR_MAP_COMPONENT.equals(componentName)) {
      return resolveCollectDataDirMapParams(params, universe, node, nodeUniverseManager, config);
    }
    if (COLLECT_THREADZ_MEMZ_RPCZ_STATS_COMPONENT.equals(componentName)) {
      return resolveCollectThreadzMemzRpczStatsParams(params, universe, node);
    }
    if (COLLECT_HEAP_SNAPSHOT_COMPONENT.equals(componentName)) {
      return resolveCollectHeapSnapshotParams(params, universe, node);
    }
    return resolveCollectCdcStateParams(params, universe, node, nodeUniverseManager);
  }

  private static List<String> resolveBashComponentParams(
      List<String> params,
      Universe universe,
      NodeDetails node,
      NodeUniverseManager nodeUniverseManager,
      Config config) {
    List<String> resolved = params;
    if (config != null) {
      String mountPath = Util.getDataDirectoryPath(universe, node, config);
      String nodeHomeDir = mountPath + "/yb-data";
      resolved = ensureFlagValue(resolved, "--mount_path", mountPath);
      resolved = ensureFlagValue(resolved, "--node_home_dir", nodeHomeDir);
    }
    String ybHomeDir = nodeUniverseManager.getYbHomeDir(node, universe);
    return ensureFlagValue(resolved, "--yb_home_dir", ybHomeDir);
  }

  private static List<String> resolveCollectDataDirMapParams(
      List<String> params,
      Universe universe,
      NodeDetails node,
      NodeUniverseManager nodeUniverseManager,
      Config config) {
    List<String> resolved = params;
    String mountPath = null;
    String nodeHomeDir = null;
    if (config != null) {
      mountPath = Util.getDataDirectoryPath(universe, node, config);
      nodeHomeDir = mountPath + "/yb-data";
      resolved = ensureFlagValue(resolved, "--mount_path", mountPath);
      resolved = ensureFlagValue(resolved, "--node_home_dir", nodeHomeDir);
    }
    String ybHomeDir = nodeUniverseManager.getYbHomeDir(node, universe);
    resolved = ensureFlagValue(resolved, "--yb_home_dir", ybHomeDir);

    String fsDataDirs =
        getOverriddenGflagValue(universe, ServerType.TSERVER, GFlagsUtil.FS_DATA_DIRS);
    if (StringUtils.isBlank(fsDataDirs) && mountPath != null) {
      fsDataDirs = mountPath;
    }
    if (StringUtils.isNotBlank(fsDataDirs)) {
      resolved = ensureFlagValue(resolved, "--fs_data_dirs", fsDataDirs);
    }
    if (nodeHomeDir != null && StringUtils.isNotBlank(fsDataDirs)) {
      String rocksdbScanRoots = String.join(",", computeRocksdbScanRoots(fsDataDirs, nodeHomeDir));
      resolved = ensureFlagValue(resolved, "--rocksdb_scan_roots", rocksdbScanRoots);
    }
    return resolved;
  }

  private static List<String> resolveCollectThreadzMemzRpczStatsParams(
      List<String> params, Universe universe, NodeDetails node) {
    List<String> resolved = params;
    String hostIp = resolveNodeHttpHost(universe, node);
    if (StringUtils.isNotBlank(hostIp)) {
      resolved = ensureFlagValue(resolved, "--host_ip", hostIp);
    }
    resolved =
        ensureFlagValue(resolved, "--tserver_http_port", String.valueOf(node.tserverHttpPort));
    LOG.debug(
        "collect_threadz_memz_rpcz_stats on node '{}': resolved_host_ip={}, tserver_http_port={}",
        node.nodeName,
        hostIp,
        node.tserverHttpPort);
    return resolved;
  }

  private static List<String> resolveCollectCdcStateParams(
      List<String> params,
      Universe universe,
      NodeDetails node,
      NodeUniverseManager nodeUniverseManager) {
    List<String> resolved = params;
    String hostIp = resolveNodeRpcHost(universe, node);
    if (StringUtils.isNotBlank(hostIp)) {
      resolved = ensureFlagValue(resolved, "--host_ip", hostIp);
    }
    resolved = ensureFlagValue(resolved, "--tserver_rpc_port", String.valueOf(node.tserverRpcPort));
    String ybHomeDir = nodeUniverseManager.getYbHomeDir(node, universe);
    resolved = ensureFlagValue(resolved, "--yb_home_dir", ybHomeDir);
    // Same directory and rule as yb-admin: a tserver with allow_insecure_connections off refuses
    // a plaintext yb-ts-cli.
    if (universe.getUniverseDetails().getPrimaryCluster().userIntent.enableNodeToNodeEncrypt) {
      resolved =
          ensureFlagValue(
              resolved,
              "--certs_dir_name",
              SupportBundleRemoteCommandBuilder.getCertsDir(nodeUniverseManager, node, universe));
    }
    LOG.debug(
        "collect_cdc_state on node '{}': host_ip={}, tserver_rpc_port={}, yb_home_dir={}",
        node.nodeName,
        hostIp,
        node.tserverRpcPort,
        ybHomeDir);
    return resolved;
  }

  private static List<String> resolveCollectHeapSnapshotParams(
      List<String> params, Universe universe, NodeDetails node) {
    List<String> resolved = params;
    String hostIp = resolveNodeHttpHost(universe, node);
    if (StringUtils.isNotBlank(hostIp)) {
      resolved = ensureFlagValue(resolved, "--host_ip", hostIp);
    }
    resolved =
        ensureFlagValue(resolved, "--tserver_http_port", String.valueOf(node.tserverHttpPort));
    resolved = ensureFlagValue(resolved, "--master_http_port", String.valueOf(node.masterHttpPort));
    resolved = ensureFlagValue(resolved, "--collect_master", node.isMaster ? "true" : "false");
    resolved = ensureFlagValue(resolved, "--collect_tserver", node.isTserver ? "true" : "false");
    LOG.debug(
        "collect_heap_snapshot on node '{}': host_ip={}, tserver_port={}, master_port={},"
            + " collect_master={}, collect_tserver={}",
        node.nodeName,
        hostIp,
        node.tserverHttpPort,
        node.masterHttpPort,
        node.isMaster,
        node.isTserver);
    return resolved;
  }

  /**
   * Host the script curls from inside the node. On kubernetes the script runs in the very pod that
   * serves the endpoint, so localhost keeps it from depending on the pod resolving its own FQDN.
   */
  private static String resolveNodeHttpHost(Universe universe, NodeDetails node) {
    if (SupportBundleK8sSupport.isK8s(universe)) {
      return LOCALHOST_IP;
    }
    if (node.cloudInfo != null) {
      if (StringUtils.isNotBlank(node.cloudInfo.private_ip)) {
        return node.cloudInfo.private_ip;
      }
      if (StringUtils.isNotBlank(node.cloudInfo.private_dns)) {
        return node.cloudInfo.private_dns;
      }
    }
    String fromUniverse = Util.getNodeIp(universe, node);
    if (StringUtils.isNotBlank(fromUniverse)) {
      return fromUniverse;
    }
    UserIntent userIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
    boolean cloudEnabled = userIntent != null && userIntent.providerType != CloudType.onprem;
    return Util.getIpToUse(universe, node, cloudEnabled);
  }

  /**
   * Host for an RPC to the node's own tserver. On kubernetes the tserver binds RPC to the pod's
   * FQDN, not localhost, so this is the address YBA itself dials.
   */
  private static String resolveNodeRpcHost(Universe universe, NodeDetails node) {
    if (SupportBundleK8sSupport.isK8s(universe)) {
      return node.cloudInfo == null ? null : node.cloudInfo.private_ip;
    }
    return resolveNodeHttpHost(universe, node);
  }

  /**
   * Every directory a tablet's RocksDB data might live under. Two layouts are emitted per
   * configured data dir because the on-disk shape differs by how the universe was created, and
   * there is no cheap way to tell from YBA which one a given node uses -- the script probes and
   * skips what does not exist. Ordered and de-duplicated so the scan is stable and a directory is
   * not visited twice.
   */
  static List<String> computeRocksdbScanRoots(String fsDataDirs, String nodeHomeDir) {
    LinkedHashSet<String> roots = new LinkedHashSet<>();
    if (StringUtils.isNotBlank(nodeHomeDir)) {
      roots.add(nodeHomeDir + "/tserver/data/rocksdb");
    }
    if (StringUtils.isBlank(fsDataDirs)) {
      return new ArrayList<>(roots);
    }
    for (String rawDir : fsDataDirs.split(",")) {
      String dir = rawDir.trim();
      if (StringUtils.isBlank(dir)) {
        continue;
      }
      roots.add(dir + "/data/rocksdb");
      roots.add(dir + "/yb-data/tserver/data/rocksdb");
    }
    return new ArrayList<>(roots);
  }
}
