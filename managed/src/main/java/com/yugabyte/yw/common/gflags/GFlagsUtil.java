// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.gflags;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.google.common.collect.ImmutableMap;
import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleConfigureServers;
import com.yugabyte.yw.common.CallHomeManager;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.certmgmt.CertificateHelper;
import com.yugabyte.yw.common.certmgmt.EncryptionInTransitUtil;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.UUID;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.function.BiConsumer;
import java.util.function.Function;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class GFlagsUtil {
  private static final Logger LOG = LoggerFactory.getLogger(GFlagsUtil.class);

  public static final String YSQL_CGROUP_PATH = "/sys/fs/cgroup/memory/ysql";

  public static final String ENABLE_YSQL = "enable_ysql";
  public static final String YSQL_ENABLE_AUTH = "ysql_enable_auth";
  public static final String START_CQL_PROXY = "start_cql_proxy";
  public static final String USE_CASSANDRA_AUTHENTICATION = "use_cassandra_authentication";
  public static final String USE_NODE_TO_NODE_ENCRYPTION = "use_node_to_node_encryption";
  public static final String USE_CLIENT_TO_SERVER_ENCRYPTION = "use_client_to_server_encryption";
  public static final String YBC_LOG_SUBDIR = "/controller/logs";
  public static final String START_REDIS_PROXY = "start_redis_proxy";

  private static final Map<String, StringIntentAccessor> GFLAG_TO_INTENT_ACCESSOR =
      ImmutableMap.<String, StringIntentAccessor>builder()
          .put(ENABLE_YSQL, boolAccessor(u -> u.enableYSQL, (u, v) -> u.enableYSQL = v))
          .put(
              YSQL_ENABLE_AUTH, boolAccessor(u -> u.enableYSQLAuth, (u, v) -> u.enableYSQLAuth = v))
          .put(START_CQL_PROXY, boolAccessor(u -> u.enableYCQL, (u, v) -> u.enableYCQL = v))
          .put(
              USE_CASSANDRA_AUTHENTICATION,
              boolAccessor(u -> u.enableYCQLAuth, (u, v) -> u.enableYCQLAuth = v))
          .put(
              USE_NODE_TO_NODE_ENCRYPTION,
              boolAccessor(u -> u.enableNodeToNodeEncrypt, (u, v) -> u.enableNodeToNodeEncrypt = v))
          .put(
              USE_CLIENT_TO_SERVER_ENCRYPTION,
              boolAccessor(
                  u -> u.enableClientToNodeEncrypt, (u, v) -> u.enableClientToNodeEncrypt = v))
          .put(START_REDIS_PROXY, boolAccessor(u -> u.enableYEDIS, (u, v) -> u.enableYEDIS = v))
          .build();

  /**
   * Return the map of default gflags which will be passed as extra gflags to the db nodes.
   *
   * @param taskParam
   * @param universe
   * @param userIntent
   * @param useHostname
   * @param config
   * @return
   */
  public static Map<String, String> getAllDefaultGFlags(
      AnsibleConfigureServers.Params taskParam,
      Universe universe,
      UniverseDefinitionTaskParams.UserIntent userIntent,
      boolean useHostname,
      Config config) {
    Map<String, String> extra_gflags = new HashMap<>();
    extra_gflags.put("placement_cloud", taskParam.getProvider().code);
    extra_gflags.put("placement_region", taskParam.getRegion().code);
    extra_gflags.put("placement_zone", taskParam.getAZ().code);
    extra_gflags.put("max_log_size", "256");
    extra_gflags.put("undefok", ENABLE_YSQL);
    extra_gflags.put("metric_node_name", taskParam.nodeName);
    extra_gflags.put("placement_uuid", String.valueOf(taskParam.placementUuid));

    String mountPoints = getMountPoints(taskParam);
    if (mountPoints != null && mountPoints.length() > 0) {
      extra_gflags.put("fs_data_dirs", mountPoints);
    } else {
      throw new RuntimeException("mountpoints and numVolumes are missing from taskParam");
    }

    NodeDetails node = universe.getNode(taskParam.nodeName);
    boolean legacyNet =
        universe.getConfig().getOrDefault(Universe.DUAL_NET_LEGACY, "true").equals("true");
    boolean isDualNet =
        config.getBoolean("yb.cloud.enabled")
            && node.cloudInfo.secondary_private_ip != null
            && !node.cloudInfo.secondary_private_ip.equals("null");
    boolean useSecondaryIp = isDualNet && !legacyNet;

    String processType = taskParam.getProperty("processType");
    if (processType == null) {
      extra_gflags.put("master_addresses", "");
    } else if (processType.equals(UniverseDefinitionTaskBase.ServerType.TSERVER.name())) {
      extra_gflags.putAll(
          getTServerDefaultGflags(
              taskParam,
              universe,
              userIntent,
              useHostname,
              useSecondaryIp,
              isDualNet,
              config.getInt(NodeManager.POSTGRES_MAX_MEM_MB) > 0));
    } else {
      extra_gflags.putAll(
          getMasterDefaultGFlags(taskParam, universe, useHostname, useSecondaryIp, isDualNet));
    }

    if (taskParam.isMaster) {
      extra_gflags.put("cluster_uuid", String.valueOf(taskParam.universeUUID));
      extra_gflags.put("replication_factor", String.valueOf(userIntent.replicationFactor));
    }

    if (taskParam.getCurrentClusterType() == UniverseDefinitionTaskParams.ClusterType.PRIMARY
        && taskParam.setTxnTableWaitCountFlag) {
      extra_gflags.put(
          "txn_table_wait_min_ts_count",
          Integer.toString(universe.getUniverseDetails().getPrimaryCluster().userIntent.numNodes));
    }

    if (taskParam.callhomeLevel != null) {
      extra_gflags.put(
          "callhome_collection_level", taskParam.callhomeLevel.toString().toLowerCase());
      if (taskParam.callhomeLevel == CallHomeManager.CollectionLevel.NONE) {
        extra_gflags.put("callhome_enabled", "false");
      }
    }

    extra_gflags.putAll(getYSQLGFlags(taskParam, universe, useHostname, useSecondaryIp));
    extra_gflags.putAll(getYCQLGFlags(taskParam, universe, useHostname, useSecondaryIp));
    extra_gflags.putAll(getCertsAndTlsGFlags(taskParam, universe));

    if (universe.getUniverseDetails().xClusterInfo.isSourceRootCertDirPathGflagConfigured()) {
      extra_gflags.put(
          XClusterConfigTaskBase.SOURCE_ROOT_CERTS_DIR_GFLAG,
          universe.getUniverseDetails().xClusterInfo.sourceRootCertDirPath);
    }

    return extra_gflags;
  }

  /** Return the map of ybc flags which will be passed to the db nodes. */
  public static Map<String, String> getYbcFlags(AnsibleConfigureServers.Params taskParam) {
    Universe universe = Universe.getOrBadRequest(taskParam.universeUUID);
    NodeDetails node = universe.getNode(taskParam.nodeName);
    UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
    String providerUUID = universeDetails.getClusterByUuid(node.placementUuid).userIntent.provider;
    Map<String, String> ybcFlags = new HashMap<>();
    ybcFlags.put("v", "1");
    ybcFlags.put("server_address", node.cloudInfo.private_ip);
    ybcFlags.put("server_port", Integer.toString(node.ybControllerRpcPort));
    ybcFlags.put("yb_tserver_address", node.cloudInfo.private_ip);
    ybcFlags.put("log_dir", getYbHomeDir(providerUUID) + YBC_LOG_SUBDIR);
    if (node.isMaster) {
      ybcFlags.put("yb_master_address", node.cloudInfo.private_ip);
    }
    if (EncryptionInTransitUtil.isRootCARequired(taskParam)) {
      String ybHomeDir = getYbHomeDir(providerUUID);
      String certsNodeDir = CertificateHelper.getCertsNodeDir(ybHomeDir);
      ybcFlags.put("certs_dir_name", certsNodeDir);
    }
    return ybcFlags;
  }

  private static String getYbHomeDir(String providerUUID) {
    if (providerUUID == null) {
      return CommonUtils.DEFAULT_YB_HOME_DIR;
    }
    return Provider.getOrBadRequest(UUID.fromString(providerUUID)).getYbHome();
  }

  private static Map<String, String> getTServerDefaultGflags(
      AnsibleConfigureServers.Params taskParam,
      Universe universe,
      UniverseDefinitionTaskParams.UserIntent userIntent,
      boolean useHostname,
      boolean useSecondaryIp,
      boolean isDualNet,
      boolean configureCGroup) {
    Map<String, String> gflags = new HashMap<>();
    NodeDetails node = universe.getNode(taskParam.nodeName);
    String masterAddresses = universe.getMasterAddresses(false, useSecondaryIp);
    String privateIp = node.cloudInfo.private_ip;

    if (useHostname) {
      gflags.put(
          "server_broadcast_addresses",
          String.format("%s:%s", privateIp, Integer.toString(node.tserverRpcPort)));
    } else {
      gflags.put("server_broadcast_addresses", "");
    }
    gflags.put(
        "rpc_bind_addresses",
        String.format("%s:%s", privateIp, Integer.toString(node.tserverRpcPort)));
    gflags.put("tserver_master_addrs", masterAddresses);

    if (useSecondaryIp) {
      String bindAddressPrimary =
          String.format("%s:%s", node.cloudInfo.private_ip, node.tserverRpcPort);
      String bindAddressSecondary =
          String.format("%s:%s", node.cloudInfo.secondary_private_ip, node.tserverRpcPort);
      String bindAddresses = bindAddressSecondary + "," + bindAddressPrimary;
      gflags.put("rpc_bind_addresses", bindAddresses);
    } else if (isDualNet) {
      // We want the broadcast address to be secondary so that
      // it gets populated correctly for the client discovery tables.
      gflags.put("server_broadcast_addresses", node.cloudInfo.secondary_private_ip);
      gflags.put("use_private_ip", "cloud");
    }

    gflags.put("webserver_port", Integer.toString(node.tserverHttpPort));
    gflags.put("webserver_interface", privateIp);
    gflags.put(
        "redis_proxy_bind_address",
        String.format("%s:%s", privateIp, Integer.toString(node.redisServerRpcPort)));
    if (userIntent.enableYEDIS) {
      gflags.put(
          "redis_proxy_webserver_port",
          Integer.toString(taskParam.communicationPorts.redisServerHttpPort));
    } else {
      gflags.put(START_REDIS_PROXY, "false");
    }
    if (configureCGroup) {
      gflags.put("postmaster_cgroup", YSQL_CGROUP_PATH);
    }
    return gflags;
  }

  private static Map<String, String> getYSQLGFlags(
      AnsibleConfigureServers.Params taskParam,
      Universe universe,
      Boolean useHostname,
      Boolean useSecondaryIp) {
    Map<String, String> gflags = new HashMap<>();
    NodeDetails node = universe.getNode(taskParam.nodeName);
    String pgsqlProxyBindAddress = node.cloudInfo.private_ip;
    if (useHostname || useSecondaryIp) {
      pgsqlProxyBindAddress = "0.0.0.0";
    }

    if (taskParam.enableYSQL) {
      gflags.put(ENABLE_YSQL, "true");
      gflags.put(
          "pgsql_proxy_bind_address",
          String.format("%s:%s", pgsqlProxyBindAddress, node.ysqlServerRpcPort));
      gflags.put(
          "pgsql_proxy_webserver_port",
          Integer.toString(taskParam.communicationPorts.ysqlServerHttpPort));
      if (taskParam.enableYSQLAuth) {
        gflags.put(YSQL_ENABLE_AUTH, "true");
        gflags.put("ysql_hba_conf_csv", "local all yugabyte trust");
      } else {
        gflags.put(YSQL_ENABLE_AUTH, "false");
      }
    } else {
      gflags.put(ENABLE_YSQL, "false");
    }
    return gflags;
  }

  private static Map<String, String> getYCQLGFlags(
      AnsibleConfigureServers.Params taskParam,
      Universe universe,
      Boolean useHostname,
      Boolean useSecondaryIp) {
    Map<String, String> gflags = new HashMap<>();
    NodeDetails node = universe.getNode(taskParam.nodeName);
    String cqlProxyBindAddress = node.cloudInfo.private_ip;
    if (useHostname || useSecondaryIp) {
      cqlProxyBindAddress = "0.0.0.0";
    }

    if (taskParam.enableYCQL) {
      gflags.put(START_CQL_PROXY, "true");
      gflags.put(
          "cql_proxy_bind_address",
          String.format("%s:%s", cqlProxyBindAddress, node.yqlServerRpcPort));
      gflags.put(
          "cql_proxy_webserver_port",
          Integer.toString(taskParam.communicationPorts.yqlServerHttpPort));
      if (taskParam.enableYCQLAuth) {
        gflags.put(USE_CASSANDRA_AUTHENTICATION, "true");
      } else {
        gflags.put(USE_CASSANDRA_AUTHENTICATION, "false");
      }
    } else {
      gflags.put(START_CQL_PROXY, "false");
    }
    return gflags;
  }

  private static Map<String, String> getCertsAndTlsGFlags(
      AnsibleConfigureServers.Params taskParam, Universe universe) {
    Map<String, String> gflags = new HashMap<>();
    NodeDetails node = universe.getNode(taskParam.nodeName);
    String nodeToNodeString = String.valueOf(taskParam.enableNodeToNodeEncrypt);
    String clientToNodeString = String.valueOf(taskParam.enableClientToNodeEncrypt);
    String allowInsecureString = String.valueOf(taskParam.allowInsecure);
    String ybHomeDir = taskParam.getProvider().getYbHome();
    String certsDir = CertificateHelper.getCertsNodeDir(ybHomeDir);
    String certsForClientDir = CertificateHelper.getCertsForClientDir(ybHomeDir);

    gflags.put(USE_NODE_TO_NODE_ENCRYPTION, nodeToNodeString);
    gflags.put(USE_CLIENT_TO_SERVER_ENCRYPTION, clientToNodeString);
    gflags.put("allow_insecure_connections", allowInsecureString);
    if (taskParam.enableClientToNodeEncrypt || taskParam.enableNodeToNodeEncrypt) {
      gflags.put("cert_node_filename", node.cloudInfo.private_ip);
    }
    if (EncryptionInTransitUtil.isRootCARequired(taskParam)) {
      gflags.put("certs_dir", certsDir);
    }
    if (EncryptionInTransitUtil.isClientRootCARequired(taskParam)) {
      gflags.put("certs_for_client_dir", certsForClientDir);
    }
    return gflags;
  }

  private static Map<String, String> getMasterDefaultGFlags(
      AnsibleConfigureServers.Params taskParam,
      Universe universe,
      Boolean useHostname,
      Boolean useSecondaryIp,
      Boolean isDualNet) {
    Map<String, String> gflags = new HashMap<>();
    NodeDetails node = universe.getNode(taskParam.nodeName);
    String masterAddresses = universe.getMasterAddresses(false, useSecondaryIp);
    String privateIp = node.cloudInfo.private_ip;

    if (useHostname) {
      gflags.put(
          "server_broadcast_addresses",
          String.format("%s:%s", privateIp, Integer.toString(node.masterRpcPort)));
    } else {
      gflags.put("server_broadcast_addresses", "");
    }

    if (!taskParam.isMasterInShellMode) {
      gflags.put("master_addresses", masterAddresses);
    } else {
      gflags.put("master_addresses", "");
    }

    gflags.put(
        "rpc_bind_addresses",
        String.format("%s:%s", privateIp, Integer.toString(node.masterRpcPort)));

    if (useSecondaryIp) {
      String bindAddressPrimary =
          String.format("%s:%s", node.cloudInfo.private_ip, node.masterRpcPort);
      String bindAddressSecondary =
          String.format("%s:%s", node.cloudInfo.secondary_private_ip, node.masterRpcPort);
      String bindAddresses = bindAddressSecondary + "," + bindAddressPrimary;
      gflags.put("rpc_bind_addresses", bindAddresses);
    } else if (isDualNet) {
      gflags.put("use_private_ip", "cloud");
    }

    gflags.put("webserver_port", Integer.toString(node.masterHttpPort));
    gflags.put("webserver_interface", privateIp);

    return gflags;
  }

  /**
   * Checks consistency between gflags and userIntent. Throws PlatformServiceException if any
   * problems are found.
   *
   * @param userIntent
   */
  public static void checkGflagsAndIntentConsistency(
      UniverseDefinitionTaskParams.UserIntent userIntent) {
    for (Map<String, String> gflags :
        Arrays.asList(userIntent.masterGFlags, userIntent.tserverGFlags)) {
      GFLAG_TO_INTENT_ACCESSOR.forEach(
          (gflagKey, accessor) -> {
            if (gflags.containsKey(gflagKey)) {
              String gflagVal = gflags.get(gflagKey);
              String intentVal = accessor.strGetter().apply(userIntent);
              if (!gflagVal.equalsIgnoreCase(intentVal)) {
                throw new PlatformServiceException(
                    BAD_REQUEST,
                    String.format(
                        "G-Flag value '%s' for '%s' is not compatible with intent value '%s'",
                        gflagVal, gflagKey, intentVal));
              }
            }
          });
    }
  }

  /**
   * Checks if there are changes in intent that will lead to gflags change.
   *
   * @param curIntent
   * @param newIntent
   * @return true if any changes are found.
   */
  public static boolean checkGFlagsByIntentChange(
      UniverseDefinitionTaskParams.UserIntent curIntent,
      UniverseDefinitionTaskParams.UserIntent newIntent) {
    for (StringIntentAccessor acc : GFLAG_TO_INTENT_ACCESSOR.values()) {
      if (!Objects.equals(acc.strGetter().apply(curIntent), acc.strGetter().apply(newIntent))) {
        return true;
      }
    }
    return false;
  }

  /**
   * Trying to synchronize certain gflags to user intent
   *
   * @param gflags
   * @param userIntent
   * @return true if any changes were applied
   */
  public static boolean syncGflagsToIntent(
      Map<String, String> gflags, UniverseDefinitionTaskParams.UserIntent userIntent) {
    AtomicBoolean result = new AtomicBoolean(false);
    GFLAG_TO_INTENT_ACCESSOR.forEach(
        (gflagKey, accessor) -> {
          if (gflags.containsKey(gflagKey)) {
            String gflagVal = gflags.get(gflagKey);
            String intentVal = accessor.strGetter().apply(userIntent);
            if (!gflagVal.equalsIgnoreCase(intentVal)) {
              LOG.info("Syncing value {} for {} into UserIntent", gflagVal, gflagKey);
              accessor.strSetter().accept(userIntent, gflagVal);
              result.set(true);
            }
          }
        });
    return result.get();
  }

  private static String getMountPoints(AnsibleConfigureServers.Params taskParam) {
    if (taskParam.deviceInfo.mountPoints != null) {
      return taskParam.deviceInfo.mountPoints;
    } else if (taskParam.deviceInfo.numVolumes != null
        && !(taskParam.getProvider().getCloudCode() == Common.CloudType.onprem)) {
      List<String> mountPoints = new ArrayList<>();
      for (int i = 0; i < taskParam.deviceInfo.numVolumes; i++) {
        mountPoints.add("/mnt/d" + i);
      }
      return String.join(",", mountPoints);
    }
    return null;
  }

  /**
   * Checks consistency between master and tserver gflags. Throws PlatformServiceException if any
   * contradictory values are found
   *
   * @param masterGFlags
   * @param tserverGFlags
   */
  public static void checkConsistency(
      Map<String, String> masterGFlags, Map<String, String> tserverGFlags) {
    for (String gflagKey : GFLAG_TO_INTENT_ACCESSOR.keySet()) {
      if (masterGFlags.containsKey(gflagKey)
          && tserverGFlags.containsKey(gflagKey)
          && !masterGFlags
              .get(gflagKey)
              .trim()
              .equalsIgnoreCase(tserverGFlags.get(gflagKey).trim())) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            String.format(
                "G-Flag value for '%s' is inconsistent between master and tserver ('%s' vs '%s')",
                gflagKey, masterGFlags.get(gflagKey), tserverGFlags.get(gflagKey)));
      }
    }
  }

  private interface StringIntentAccessor {
    Function<UniverseDefinitionTaskParams.UserIntent, String> strGetter();

    BiConsumer<UniverseDefinitionTaskParams.UserIntent, String> strSetter();
  }

  private static StringIntentAccessor boolAccessor(
      Function<UniverseDefinitionTaskParams.UserIntent, Boolean> getter,
      BiConsumer<UniverseDefinitionTaskParams.UserIntent, Boolean> setter) {
    return new StringIntentAccessor() {
      @Override
      public Function<UniverseDefinitionTaskParams.UserIntent, String> strGetter() {
        return (intent) -> getter.apply(intent).toString();
      }

      @Override
      public BiConsumer<UniverseDefinitionTaskParams.UserIntent, String> strSetter() {
        return (userIntent, s) -> setter.accept(userIntent, Boolean.valueOf(s));
      }
    };
  }
}
