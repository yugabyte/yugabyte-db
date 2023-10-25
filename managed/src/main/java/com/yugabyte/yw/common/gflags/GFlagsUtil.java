// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.gflags;

import static com.yugabyte.yw.common.Util.getDataDirectoryPath;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.google.common.collect.ImmutableSet;
import com.typesafe.config.Config;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleConfigureServers;
import com.yugabyte.yw.common.CallHomeManager;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.certmgmt.CertificateHelper;
import com.yugabyte.yw.common.certmgmt.EncryptionInTransitUtil;
import com.yugabyte.yw.common.config.CustomerConfKeys;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.utils.FileUtils;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.audit.AuditLogConfig;
import com.yugabyte.yw.models.helpers.audit.YCQLAuditConfig;
import com.yugabyte.yw.models.helpers.audit.YSQLAuditConfig;
import java.io.IOException;
import java.io.StringReader;
import java.io.StringWriter;
import java.net.URI;
import java.net.URISyntaxException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.NoSuchAlgorithmException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collection;
import java.util.HashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import java.util.TreeMap;
import java.util.UUID;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.function.BiConsumer;
import java.util.function.Function;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.stream.Collectors;
import javax.annotation.Nullable;
import org.apache.commons.collections.CollectionUtils;
import org.apache.commons.collections.MapUtils;
import org.apache.commons.csv.CSVFormat;
import org.apache.commons.csv.CSVParser;
import org.apache.commons.csv.CSVPrinter;
import org.apache.commons.csv.CSVRecord;
import org.apache.commons.lang3.StringUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

public class GFlagsUtil {
  private static final Logger LOG = LoggerFactory.getLogger(GFlagsUtil.class);

  // This is not the full path to the cgroup. That is determined by ansible, allowing seamless
  // handling of both cgroup v1 and v2.
  public static final String YSQL_CGROUP_PATH = "ysql";

  private static final int DEFAULT_MAX_MEMORY_USAGE_PCT_FOR_DEDICATED = 90;

  public static final String DEFAULT_MEMORY_LIMIT_TO_RAM_RATIO =
      "default_memory_limit_to_ram_ratio";
  public static final String ENABLE_YSQL = "enable_ysql";
  public static final String YSQL_ENABLE_AUTH = "ysql_enable_auth";
  public static final String START_CQL_PROXY = "start_cql_proxy";
  public static final String USE_CASSANDRA_AUTHENTICATION = "use_cassandra_authentication";
  public static final String USE_NODE_TO_NODE_ENCRYPTION = "use_node_to_node_encryption";
  public static final String USE_CLIENT_TO_SERVER_ENCRYPTION = "use_client_to_server_encryption";
  public static final String START_REDIS_PROXY = "start_redis_proxy";

  public static final String VERIFY_SERVER_ENDPOINT_GFLAG = "verify_server_endpoint";
  public static final String PLACEMENT_CLOUD = "placement_cloud";
  public static final String PLACEMENT_REGION = "placement_region";
  public static final String PLACEMENT_ZONE = "placement_zone";
  public static final String MAX_LOG_SIZE = "max_log_size";
  public static final String UNDEFOK = "undefok";
  public static final String METRIC_NODE_NAME = "metric_node_name";
  public static final String PLACEMENT_UUID = "placement_uuid";
  public static final String FS_DATA_DIRS = "fs_data_dirs";
  public static final String MASTER_ADDRESSES = "master_addresses";
  public static final String CLUSTER_UUID = "cluster_uuid";
  public static final String REPLICATION_FACTOR = "replication_factor";
  public static final String TXN_TABLE_WAIT_MIN_TS_COUNT = "txn_table_wait_min_ts_count";
  public static final String CALLHOME_COLLECTION_LEVEL = "callhome_collection_level";
  public static final String CALLHOME_ENABLED = "callhome_enabled";
  public static final String USE_NODE_HOSTNAME_FOR_LOCAL_TSERVER =
      "use_node_hostname_for_local_tserver";
  public static final String SERVER_BROADCAST_ADDRESSES = "server_broadcast_addresses";
  public static final String RPC_BIND_ADDRESSES = "rpc_bind_addresses";
  public static final String TSERVER_MASTER_ADDRS = "tserver_master_addrs";
  public static final String USE_PRIVATE_IP = "use_private_ip";
  public static final String WEBSERVER_PORT = "webserver_port";
  public static final String WEBSERVER_INTERFACE = "webserver_interface";
  public static final String REDIS_PROXY_BIND_ADDRESS = "redis_proxy_bind_address";
  public static final String REDIS_PROXY_WEBSERVER_PORT = "redis_proxy_webserver_port";
  public static final String POSTMASTER_CGROUP = "postmaster_cgroup";
  public static final String PSQL_PROXY_BIND_ADDRESS = "pgsql_proxy_bind_address";
  public static final String PSQL_PROXY_WEBSERVER_PORT = "pgsql_proxy_webserver_port";
  public static final String YSQL_HBA_CONF_CSV = "ysql_hba_conf_csv";
  public static final String YSQL_PG_CONF_CSV = "ysql_pg_conf_csv";
  public static final String CSQL_PROXY_BIND_ADDRESS = "cql_proxy_bind_address";
  public static final String CSQL_PROXY_WEBSERVER_PORT = "cql_proxy_webserver_port";
  public static final String ALLOW_INSECURE_CONNECTIONS = "allow_insecure_connections";
  public static final String CERT_NODE_FILENAME = "cert_node_filename";
  public static final String CERTS_DIR = "certs_dir";
  public static final String CERTS_FOR_CLIENT_DIR = "certs_for_client_dir";
  public static final String WEBSERVER_REDIRECT_HTTP_TO_HTTPS = "webserver_redirect_http_to_https";
  public static final String WEBSERVER_CERTIFICATE_FILE = "webserver_certificate_file";
  public static final String WEBSERVER_PRIVATE_KEY_FILE = "webserver_private_key_file";
  public static final String WEBSERVER_CA_CERTIFICATE_FILE = "webserver_ca_certificate_file";
  public static final String RAFT_HEARTBEAT_INTERVAL = "raft_heartbeat_interval_ms";
  public static final String LEADER_LEASE_DURATION_MS = "leader_lease_duration_ms";
  public static final String LEADER_FAILURE_MAX_MISSED_HEARTBEAT_PERIODS =
      "leader_failure_max_missed_heartbeat_periods";

  public static final String YBC_LOG_SUBDIR = "/controller/logs";
  public static final String CORES_DIR_PATH = "/cores";

  public static final String K8S_MNT_PATH = "/mnt/disk0";
  public static final String K8S_YBC_DATA = "/ybc-data";
  public static final String K8S_YBC_LOG_SUBDIR = K8S_MNT_PATH + K8S_YBC_DATA + YBC_LOG_SUBDIR;
  public static final String K8S_YBC_CORES_DIR = K8S_MNT_PATH + CORES_DIR_PATH;

  public static final String TSERVER_DIR = "/tserver";
  public static final String POSTGRES_BIN_DIR = "/postgres/bin";
  public static final String TSERVER_BIN_DIR = TSERVER_DIR + "/bin";
  public static final String TSERVER_POSTGRES_BIN_DIR = TSERVER_DIR + POSTGRES_BIN_DIR;
  public static final String YB_ADMIN_PATH = TSERVER_BIN_DIR + "/yb-admin";
  public static final String YB_CTL_PATH = TSERVER_BIN_DIR + "/yb-ctl";
  public static final String YSQL_DUMP_PATH = TSERVER_POSTGRES_BIN_DIR + "/ysql_dump";
  public static final String YSQL_DUMPALL_PATH = TSERVER_POSTGRES_BIN_DIR + "/ysql_dumpall";
  public static final String YSQLSH_PATH = TSERVER_POSTGRES_BIN_DIR + "/ysqlsh";
  public static final String YCQLSH_PATH = TSERVER_BIN_DIR + "/ycqlsh";
  public static final String REDIS_CLI_PATH = TSERVER_BIN_DIR + "/redis-cli";
  public static final String YBC_MAX_CONCURRENT_UPLOADS = "max_concurrent_uploads";
  public static final String YBC_MAX_CONCURRENT_DOWNLOADS = "max_concurrent_downloads";
  public static final String YBC_PER_UPLOAD_OBJECTS = "per_upload_num_objects";
  public static final String YBC_PER_DOWNLOAD_OBJECTS = "per_download_num_objects";
  public static final String TMP_DIRECTORY = "tmp_dir";
  public static final String JWKS_FILE_CONTENT_KEY = "jwks=";
  public static final String JWT_AUDIENCES = "jwt_audiences=";
  public static final String JWT_ISSUERS = "jwt_issuers=";
  public static final String JWT_MATCHING_CLAIM_KEY = "jwt_matching_claim_key=";
  public static final String JWT_JWKS_FILE_PATH = "jwt_jwks_path";
  public static final String JWT_AUTH = "jwt";
  public static final String GFLAG_REMOTE_FILES_PATH = TSERVER_DIR + "/conf/gflag_files/";

  private static final Set<String> GFLAGS_FORBIDDEN_TO_OVERRIDE =
      ImmutableSet.<String>builder()
          .add(PLACEMENT_CLOUD)
          .add(PLACEMENT_REGION)
          .add(PLACEMENT_ZONE)
          .add(PLACEMENT_UUID)
          .add(FS_DATA_DIRS)
          .add(MASTER_ADDRESSES)
          .add(CLUSTER_UUID)
          .add(REPLICATION_FACTOR)
          .add(TXN_TABLE_WAIT_MIN_TS_COUNT)
          .add(USE_NODE_HOSTNAME_FOR_LOCAL_TSERVER)
          .add(SERVER_BROADCAST_ADDRESSES)
          .add(RPC_BIND_ADDRESSES)
          .add(TSERVER_MASTER_ADDRS)
          .add(USE_PRIVATE_IP)
          .add(WEBSERVER_PORT)
          .add(WEBSERVER_INTERFACE)
          .add(POSTMASTER_CGROUP)
          .add(ALLOW_INSECURE_CONNECTIONS)
          .add(CERT_NODE_FILENAME)
          .add(CERTS_DIR)
          .add(CERTS_FOR_CLIENT_DIR)
          .build();

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
    Map<String, String> extra_gflags = new TreeMap<>();
    extra_gflags.put(PLACEMENT_CLOUD, taskParam.getProvider().getCode());
    extra_gflags.put(PLACEMENT_REGION, taskParam.getRegion().getCode());
    extra_gflags.put(PLACEMENT_ZONE, taskParam.getAZ().getCode());
    extra_gflags.put(MAX_LOG_SIZE, "256");
    extra_gflags.put(UNDEFOK, ENABLE_YSQL);
    extra_gflags.put(METRIC_NODE_NAME, taskParam.nodeName);
    extra_gflags.put(PLACEMENT_UUID, String.valueOf(taskParam.placementUuid));

    String mountPoints = getMountPoints(taskParam);
    if (mountPoints != null && mountPoints.length() > 0) {
      extra_gflags.put(FS_DATA_DIRS, mountPoints);
    } else {
      throw new RuntimeException("mountpoints and numVolumes are missing from taskParam");
    }

    boolean isMultiRegion =
        universe.getConfig().getOrDefault(Universe.IS_MULTIREGION, "false").equals("true");
    if (isMultiRegion) {
      extra_gflags.put(RAFT_HEARTBEAT_INTERVAL, String.valueOf(1500));
      extra_gflags.put(LEADER_LEASE_DURATION_MS, String.valueOf(6000));
      extra_gflags.put(LEADER_FAILURE_MAX_MISSED_HEARTBEAT_PERIODS, String.valueOf(5));
    }

    NodeDetails node = universe.getNode(taskParam.nodeName);
    boolean legacyNet =
        universe.getConfig().getOrDefault(Universe.DUAL_NET_LEGACY, "true").equals("true");
    boolean isDualNet =
        config.getBoolean("yb.cloud.enabled")
            && node.cloudInfo.secondary_private_ip != null
            && !node.cloudInfo.secondary_private_ip.equals("null");
    boolean useSecondaryIp = isDualNet && !legacyNet;

    if (node.dedicatedTo != null) {
      extra_gflags.put(
          DEFAULT_MEMORY_LIMIT_TO_RAM_RATIO,
          String.valueOf(DEFAULT_MAX_MEMORY_USAGE_PCT_FOR_DEDICATED));
    }

    String processType = taskParam.getProperty("processType");
    if (processType == null) {
      extra_gflags.put(MASTER_ADDRESSES, "");
    } else if (processType.equals(UniverseTaskBase.ServerType.TSERVER.name())) {
      boolean configCgroup = config.getInt(NodeManager.POSTGRES_MAX_MEM_MB) > 0;

      // If the cluster is a read replica, use the read replica max mem value if its >= 0. -1 means
      // to use the primary cluster value instead.
      if (universe.getUniverseDetails().getClusterByUuid(taskParam.placementUuid).clusterType
              == UniverseDefinitionTaskParams.ClusterType.ASYNC
          && config.getInt(NodeManager.POSTGRES_RR_MAX_MEM_MB) >= 0) {
        configCgroup = config.getInt(NodeManager.POSTGRES_RR_MAX_MEM_MB) > 0;
      }
      extra_gflags.putAll(
          getTServerDefaultGflags(
              taskParam,
              universe,
              userIntent,
              useHostname,
              useSecondaryIp,
              isDualNet,
              configCgroup));
    } else {
      extra_gflags.putAll(
          getMasterDefaultGFlags(taskParam, universe, useHostname, useSecondaryIp, isDualNet));
    }

    // Set on both master and tserver processes to allow db to validate inter-node RPCs.
    extra_gflags.put(CLUSTER_UUID, String.valueOf(taskParam.getUniverseUUID()));

    if (taskParam.isMaster) {
      extra_gflags.put(REPLICATION_FACTOR, String.valueOf(userIntent.replicationFactor));
    }

    if (taskParam.getCurrentClusterType() == UniverseDefinitionTaskParams.ClusterType.PRIMARY
        && taskParam.setTxnTableWaitCountFlag) {
      extra_gflags.put(
          TXN_TABLE_WAIT_MIN_TS_COUNT,
          Integer.toString(universe.getUniverseDetails().getPrimaryCluster().userIntent.numNodes));
    }

    if (taskParam.callhomeLevel != null) {
      extra_gflags.put(CALLHOME_COLLECTION_LEVEL, taskParam.callhomeLevel.toString().toLowerCase());
      if (taskParam.callhomeLevel == CallHomeManager.CollectionLevel.NONE) {
        extra_gflags.put(CALLHOME_ENABLED, "false");
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
  public static Map<String, String> getYbcFlags(
      Universe universe,
      AnsibleConfigureServers.Params taskParam,
      RuntimeConfGetter confGetter,
      Config config) {
    NodeDetails node = universe.getNode(taskParam.nodeName);
    // Both for old clusters and new, binding to both IPs works.
    boolean isDualNet =
        confGetter.getConfForScope(
                Customer.get(universe.getCustomerId()), CustomerConfKeys.cloudEnabled)
            && node.cloudInfo.secondary_private_ip != null
            && !node.cloudInfo.secondary_private_ip.equals("null");
    String serverAddresses =
        isDualNet
            ? String.format("%s,%s", node.cloudInfo.private_ip, node.cloudInfo.secondary_private_ip)
            : node.cloudInfo.private_ip;

    UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
    UserIntent userIntent = universeDetails.getClusterByUuid(node.placementUuid).userIntent;
    String providerUUID = userIntent.provider;
    Map<String, String> ybcFlags = new TreeMap<>();
    ybcFlags.put("v", Integer.toString(1));
    ybcFlags.put("server_address", serverAddresses);
    ybcFlags.put("server_port", Integer.toString(node.ybControllerRpcPort));
    ybcFlags.put("log_dir", getYbHomeDir(providerUUID) + YBC_LOG_SUBDIR);
    ybcFlags.put("cores_dir", getYbHomeDir(providerUUID) + CORES_DIR_PATH);

    ybcFlags.put("yb_master_address", node.cloudInfo.private_ip);
    ybcFlags.put("yb_master_webserver_port", Integer.toString(node.masterHttpPort));
    ybcFlags.put("yb_tserver_webserver_port", Integer.toString(node.tserverHttpPort));

    // For "yb_tserver_address", private_ip works for cloud cases,
    // since pgsql_bind_address is set to 0.0.0.0 or private_ip.
    // Also, /varz endpoint works, since webserver_interface is set to private_ip.
    ybcFlags.put("yb_tserver_address", node.cloudInfo.private_ip);
    ybcFlags.put("redis_cli", getYbHomeDir(providerUUID) + REDIS_CLI_PATH);
    ybcFlags.put("yb_admin", getYbHomeDir(providerUUID) + YB_ADMIN_PATH);
    ybcFlags.put("yb_ctl", getYbHomeDir(providerUUID) + YB_CTL_PATH);
    ybcFlags.put("ysql_dump", getYbHomeDir(providerUUID) + YSQL_DUMP_PATH);
    ybcFlags.put("ysql_dumpall", getYbHomeDir(providerUUID) + YSQL_DUMPALL_PATH);
    ybcFlags.put("ysqlsh", getYbHomeDir(providerUUID) + YSQLSH_PATH);
    ybcFlags.put("ycqlsh", getYbHomeDir(providerUUID) + YCQLSH_PATH);

    if (taskParam.enableNodeToNodeEncrypt) {
      ybcFlags.put(CERT_NODE_FILENAME, node.cloudInfo.private_ip);
    }
    if (MapUtils.isNotEmpty(userIntent.ybcFlags)) {
      ybcFlags.putAll(userIntent.ybcFlags);
    }
    // Append the custom_tmp gflag to the YBC gflag.
    String ybcTempDir = GFlagsUtil.getCustomTmpDirectory(node, universe);
    // PLAT-10007 use ybc-data instead of /tmp
    if (ybcTempDir.equals("/tmp")) {
      ybcTempDir = getDataDirectoryPath(universe, node, config) + "/ybc-data";
    }
    ybcFlags.put(TMP_DIRECTORY, ybcTempDir);
    if (EncryptionInTransitUtil.isRootCARequired(taskParam)) {
      String ybHomeDir = getYbHomeDir(providerUUID);
      String certsNodeDir = CertificateHelper.getCertsNodeDir(ybHomeDir);
      ybcFlags.put("certs_dir_name", certsNodeDir);
    }
    boolean enableVerbose =
        confGetter.getConfForScope(universe, UniverseConfKeys.ybcEnableVervbose);
    if (enableVerbose) {
      ybcFlags.put("v", "1");
    }
    String nfsDirs = confGetter.getConfForScope(universe, UniverseConfKeys.nfsDirs);
    ybcFlags.put("nfs_dirs", nfsDirs);
    return ybcFlags;
  }

  /** Return the map of ybc flags which will be passed to the db nodes. */
  public static Map<String, String> getYbcFlagsForK8s(
      UUID universeUUID, String nodeName, boolean listenOnAllInterfaces, int hardwareConcurrency) {
    Universe universe = Universe.getOrBadRequest(universeUUID);
    NodeDetails node = universe.getNode(nodeName);
    UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
    UserIntent userIntent = universeDetails.getClusterByUuid(node.placementUuid).userIntent;
    String providerUUID = userIntent.provider;
    Provider provider = Provider.getOrBadRequest(UUID.fromString(providerUUID));
    String ybHomeDir = provider.getYbHome();
    String serverAddress =
        listenOnAllInterfaces
            ? (userIntent.enableIPV6 ? "[::]" : "0.0.0.0")
            : node.cloudInfo.private_ip;
    Map<String, String> ybcFlags = new TreeMap<>();
    ybcFlags.put("v", Integer.toString(1));
    ybcFlags.put("hardware_concurrency", Integer.toString(hardwareConcurrency));
    ybcFlags.put("server_address", serverAddress);
    ybcFlags.put("server_port", Integer.toString(node.ybControllerRpcPort));
    ybcFlags.put("log_dir", K8S_YBC_LOG_SUBDIR);
    ybcFlags.put("cores_dir", K8S_YBC_CORES_DIR);
    ybcFlags.put("yb_master_webserver_port", Integer.toString(node.masterHttpPort));
    ybcFlags.put("yb_tserver_webserver_port", Integer.toString(node.tserverHttpPort));
    ybcFlags.put("yb_tserver_address", node.cloudInfo.private_ip);
    ybcFlags.put("redis_cli", ybHomeDir + REDIS_CLI_PATH);
    ybcFlags.put("yb_admin", ybHomeDir + YB_ADMIN_PATH);
    ybcFlags.put("yb_ctl", ybHomeDir + YB_CTL_PATH);
    ybcFlags.put("ysql_dump", ybHomeDir + YSQL_DUMP_PATH);
    ybcFlags.put("ysql_dumpall", ybHomeDir + YSQL_DUMPALL_PATH);
    ybcFlags.put("ysqlsh", ybHomeDir + YSQLSH_PATH);
    ybcFlags.put("ycqlsh", ybHomeDir + YCQLSH_PATH);

    if (MapUtils.isNotEmpty(userIntent.ybcFlags)) {
      ybcFlags.putAll(userIntent.ybcFlags);
    }
    if (EncryptionInTransitUtil.isRootCARequired(universeDetails)) {
      ybcFlags.put("certs_dir_name", "/opt/certs/yugabyte");
      ybcFlags.put("cert_node_filename", node.cloudInfo.private_ip);
    }
    return ybcFlags;
  }

  public static String getYbHomeDir(String providerUUID) {
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
    Map<String, String> gflags = new TreeMap<>();
    NodeDetails node = universe.getNode(taskParam.nodeName);
    String masterAddresses = universe.getMasterAddresses(false, useSecondaryIp);
    String privateIp = node.cloudInfo.private_ip;

    if (useHostname) {
      gflags.put(
          SERVER_BROADCAST_ADDRESSES,
          String.format("%s:%s", privateIp, Integer.toString(node.tserverRpcPort)));
      gflags.put(USE_NODE_HOSTNAME_FOR_LOCAL_TSERVER, "true");
    } else {
      gflags.put(SERVER_BROADCAST_ADDRESSES, "");
    }
    gflags.put(
        RPC_BIND_ADDRESSES,
        String.format("%s:%s", privateIp, Integer.toString(node.tserverRpcPort)));
    gflags.put(TSERVER_MASTER_ADDRS, masterAddresses);

    if (useSecondaryIp) {
      String bindAddressPrimary =
          String.format("%s:%s", node.cloudInfo.private_ip, node.tserverRpcPort);
      String bindAddressSecondary =
          String.format("%s:%s", node.cloudInfo.secondary_private_ip, node.tserverRpcPort);
      String bindAddresses = bindAddressSecondary + "," + bindAddressPrimary;
      gflags.put(RPC_BIND_ADDRESSES, bindAddresses);
    } else if (isDualNet) {
      // We want the broadcast address to be secondary so that
      // it gets populated correctly for the client discovery tables.
      gflags.put(SERVER_BROADCAST_ADDRESSES, node.cloudInfo.secondary_private_ip);
      gflags.put(USE_PRIVATE_IP, "cloud");
    }

    gflags.put(WEBSERVER_PORT, Integer.toString(node.tserverHttpPort));
    gflags.put(WEBSERVER_INTERFACE, privateIp);
    gflags.put(
        REDIS_PROXY_BIND_ADDRESS,
        String.format("%s:%s", privateIp, Integer.toString(node.redisServerRpcPort)));
    if (userIntent.enableYEDIS) {
      gflags.put(
          REDIS_PROXY_WEBSERVER_PORT,
          Integer.toString(taskParam.communicationPorts.redisServerHttpPort));
    } else {
      gflags.put(START_REDIS_PROXY, "false");
    }
    if (configureCGroup) {
      gflags.put(POSTMASTER_CGROUP, YSQL_CGROUP_PATH);
    }
    return gflags;
  }

  public static String getCustomTmpDirectory(NodeDetails node, Universe universe) {
    Map<String, String> res = new HashMap<>();
    UniverseDefinitionTaskParams.Cluster cluster = universe.getCluster(node.placementUuid);
    if (node.isMaster) {
      res.putAll(
          getGFlagsForNode(
              node,
              UniverseTaskBase.ServerType.MASTER,
              cluster,
              universe.getUniverseDetails().clusters));
    }
    if (node.isTserver) {
      res.putAll(
          getGFlagsForNode(
              node,
              UniverseTaskBase.ServerType.TSERVER,
              cluster,
              universe.getUniverseDetails().clusters));
    }

    return res.getOrDefault(TMP_DIRECTORY, "/tmp");
  }

  private static Map<String, String> getYSQLGFlags(
      AnsibleConfigureServers.Params taskParam,
      Universe universe,
      Boolean useHostname,
      Boolean useSecondaryIp) {
    Map<String, String> gflags = new TreeMap<>();
    NodeDetails node = universe.getNode(taskParam.nodeName);
    String pgsqlProxyBindAddress = node.cloudInfo.private_ip;
    if (useHostname || useSecondaryIp) {
      pgsqlProxyBindAddress = "0.0.0.0";
    }

    if (taskParam.enableYSQL) {
      gflags.put(ENABLE_YSQL, "true");
      gflags.put(
          PSQL_PROXY_BIND_ADDRESS,
          String.format("%s:%s", pgsqlProxyBindAddress, node.ysqlServerRpcPort));
      gflags.put(PSQL_PROXY_WEBSERVER_PORT, Integer.toString(node.ysqlServerHttpPort));
      if (taskParam.enableYSQLAuth) {
        gflags.put(YSQL_ENABLE_AUTH, "true");
        gflags.put(YSQL_HBA_CONF_CSV, "local all yugabyte trust");
      } else {
        gflags.put(YSQL_ENABLE_AUTH, "false");
      }
      String ysqlPgConfCsv = getYsqlPgConfCsv(universe);
      if (StringUtils.isNotEmpty(ysqlPgConfCsv)) {
        gflags.put(YSQL_PG_CONF_CSV, ysqlPgConfCsv);
      }
    } else {
      gflags.put(ENABLE_YSQL, "false");
    }
    return gflags;
  }

  private static String getYsqlPgConfCsv(Universe universe) {
    List<String> ysqlPgConfCsvEntries = new ArrayList<>();
    AuditLogConfig auditLogConfig =
        universe.getUniverseDetails().getPrimaryCluster().userIntent.auditLogConfig;
    if (auditLogConfig != null) {
      if (auditLogConfig.getYsqlAuditConfig() != null
          && auditLogConfig.getYcqlAuditConfig().isEnabled()) {
        YSQLAuditConfig ysqlAuditConfig = auditLogConfig.getYsqlAuditConfig();
        if (CollectionUtils.isNotEmpty(ysqlAuditConfig.getClasses())) {
          ysqlPgConfCsvEntries.add(
              "\"pgaudit.log='"
                  + ysqlAuditConfig.getClasses().stream()
                      .map(YSQLAuditConfig.YSQLAuditStatementClass::name)
                      .collect(Collectors.joining(","))
                  + "'\"");
        }
        ysqlPgConfCsvEntries.add(
            encodeBooleanPgAuditFlag("pgaudit.log_catalog", ysqlAuditConfig.isLogCatalog()));
        ysqlPgConfCsvEntries.add(
            encodeBooleanPgAuditFlag("pgaudit.log_client", ysqlAuditConfig.isLogClient()));
        if (ysqlAuditConfig.getLogLevel() != null) {
          ysqlPgConfCsvEntries.add("pgaudit.log_level=" + ysqlAuditConfig.getLogLevel().name());
        }
        ysqlPgConfCsvEntries.add(
            encodeBooleanPgAuditFlag("pgaudit.log_parameter", ysqlAuditConfig.isLogParameter()));
        if (ysqlAuditConfig.getLogParameterMaxSize() != null) {
          ysqlPgConfCsvEntries.add(
              "pgaudit.log_parameter_max_size=" + ysqlAuditConfig.getLogParameterMaxSize());
        }
        ysqlPgConfCsvEntries.add(
            encodeBooleanPgAuditFlag("pgaudit.log_relation", ysqlAuditConfig.isLogRelation()));
        ysqlPgConfCsvEntries.add(
            encodeBooleanPgAuditFlag("pgaudit.log_row", ysqlAuditConfig.isLogRow()));
        ysqlPgConfCsvEntries.add(
            encodeBooleanPgAuditFlag("pgaudit.log_statement", ysqlAuditConfig.isLogStatement()));
        ysqlPgConfCsvEntries.add(
            encodeBooleanPgAuditFlag(
                "pgaudit.log_statement_once", ysqlAuditConfig.isLogStatementOnce()));
      }
    }
    return String.join(",", ysqlPgConfCsvEntries);
  }

  private static String encodeBooleanPgAuditFlag(String flag, boolean value) {
    return flag + "=" + (value ? "ON" : "OFF");
  }

  private static Map<String, String> getYCQLGFlags(
      AnsibleConfigureServers.Params taskParam,
      Universe universe,
      Boolean useHostname,
      Boolean useSecondaryIp) {
    Map<String, String> gflags = new TreeMap<>();
    NodeDetails node = universe.getNode(taskParam.nodeName);
    String cqlProxyBindAddress = node.cloudInfo.private_ip;
    if (useHostname || useSecondaryIp) {
      cqlProxyBindAddress = "0.0.0.0";
    }

    if (taskParam.enableYCQL) {
      gflags.put(START_CQL_PROXY, "true");
      gflags.put(
          CSQL_PROXY_BIND_ADDRESS,
          String.format("%s:%s", cqlProxyBindAddress, node.yqlServerRpcPort));
      gflags.put(CSQL_PROXY_WEBSERVER_PORT, Integer.toString(node.yqlServerHttpPort));
      if (taskParam.enableYCQLAuth) {
        gflags.put(USE_CASSANDRA_AUTHENTICATION, "true");
      } else {
        gflags.put(USE_CASSANDRA_AUTHENTICATION, "false");
      }
      gflags.putAll(getYcqlAuditFlags(universe));
    } else {
      gflags.put(START_CQL_PROXY, "false");
    }
    return gflags;
  }

  private static Map<String, String> getYcqlAuditFlags(Universe universe) {
    Map<String, String> result = new HashMap<>();
    AuditLogConfig auditLogConfig =
        universe.getUniverseDetails().getPrimaryCluster().userIntent.getAuditLogConfig();
    if (auditLogConfig != null) {
      if (auditLogConfig.getYcqlAuditConfig() != null
          && auditLogConfig.getYcqlAuditConfig().isEnabled()) {
        YCQLAuditConfig ycqlAuditConfig = auditLogConfig.getYcqlAuditConfig();
        if (CollectionUtils.isNotEmpty(ycqlAuditConfig.getIncludedCategories())) {
          result.put(
              "ycql_audit_included_categories",
              ycqlAuditConfig.getIncludedCategories().stream()
                  .map(YCQLAuditConfig.YCQLAuditCategory::name)
                  .collect(Collectors.joining(",")));
        }
        if (CollectionUtils.isNotEmpty(ycqlAuditConfig.getExcludedCategories())) {
          result.put(
              "ycql_audit_excluded_categories",
              ycqlAuditConfig.getExcludedCategories().stream()
                  .map(YCQLAuditConfig.YCQLAuditCategory::name)
                  .collect(Collectors.joining(",")));
        }
        if (CollectionUtils.isNotEmpty(ycqlAuditConfig.getIncludedUsers())) {
          result.put(
              "ycql_audit_included_users", String.join(",", ycqlAuditConfig.getIncludedUsers()));
        }
        if (CollectionUtils.isNotEmpty(ycqlAuditConfig.getExcludedUsers())) {
          result.put(
              "ycql_audit_excluded_users", String.join(",", ycqlAuditConfig.getExcludedUsers()));
        }
        if (CollectionUtils.isNotEmpty(ycqlAuditConfig.getIncludedKeyspaces())) {
          result.put(
              "ycql_audit_included_keyspaces",
              String.join(",", ycqlAuditConfig.getIncludedKeyspaces()));
        }
        if (CollectionUtils.isNotEmpty(ycqlAuditConfig.getExcludedCategories())) {
          result.put(
              "ycql_audit_excluded_keyspaces",
              String.join(",", ycqlAuditConfig.getExcludedKeyspaces()));
        }
        if (ycqlAuditConfig.getLogLevel() != null) {
          result.put("ycql_audit_log_level", ycqlAuditConfig.getLogLevel().name());
        }
      }
    }
    return result;
  }

  public static Map<String, String> getCertsAndTlsGFlags(
      AnsibleConfigureServers.Params taskParam, Universe universe) {
    Map<String, String> gflags = new TreeMap<>();
    NodeDetails node = universe.getNode(taskParam.nodeName);
    String nodeToNodeString = String.valueOf(taskParam.enableNodeToNodeEncrypt);
    String clientToNodeString = String.valueOf(taskParam.enableClientToNodeEncrypt);
    String allowInsecureString = String.valueOf(taskParam.allowInsecure);
    String ybHomeDir = taskParam.getProvider().getYbHome();
    String certsDir = CertificateHelper.getCertsNodeDir(ybHomeDir);
    String certsForClientDir = CertificateHelper.getCertsForClientDir(ybHomeDir);

    gflags.put(USE_NODE_TO_NODE_ENCRYPTION, nodeToNodeString);
    gflags.put(USE_CLIENT_TO_SERVER_ENCRYPTION, clientToNodeString);
    gflags.put(ALLOW_INSECURE_CONNECTIONS, allowInsecureString);
    if (taskParam.enableClientToNodeEncrypt || taskParam.enableNodeToNodeEncrypt) {
      gflags.put(CERT_NODE_FILENAME, node.cloudInfo.private_ip);
    }
    if (EncryptionInTransitUtil.isRootCARequired(taskParam)) {
      gflags.put(CERTS_DIR, certsDir);
    }
    if (EncryptionInTransitUtil.isClientRootCARequired(taskParam)) {
      gflags.put(CERTS_FOR_CLIENT_DIR, certsForClientDir);
    }

    boolean httpsEnabledUI =
        universe.getConfig().getOrDefault(Universe.HTTPS_ENABLED_UI, "false").equals("true");
    if (httpsEnabledUI) {
      gflags.put(
          WEBSERVER_CERTIFICATE_FILE,
          String.format("%s/node.%s.crt", certsDir, node.cloudInfo.private_ip));
      gflags.put(
          WEBSERVER_PRIVATE_KEY_FILE,
          String.format("%s/node.%s.key", certsDir, node.cloudInfo.private_ip));
      gflags.put(WEBSERVER_CA_CERTIFICATE_FILE, String.format("%s/ca.crt", certsDir));
      gflags.put(WEBSERVER_REDIRECT_HTTP_TO_HTTPS, "true");
    }
    return gflags;
  }

  private static Map<String, String> getMasterDefaultGFlags(
      AnsibleConfigureServers.Params taskParam,
      Universe universe,
      Boolean useHostname,
      Boolean useSecondaryIp,
      Boolean isDualNet) {
    Map<String, String> gflags = new TreeMap<>();
    NodeDetails node = universe.getNode(taskParam.nodeName);
    String masterAddresses = universe.getMasterAddresses(false, useSecondaryIp);
    String privateIp = node.cloudInfo.private_ip;

    if (useHostname) {
      gflags.put(
          SERVER_BROADCAST_ADDRESSES,
          String.format("%s:%s", privateIp, Integer.toString(node.masterRpcPort)));
      gflags.put(USE_NODE_HOSTNAME_FOR_LOCAL_TSERVER, "true");
    } else {
      gflags.put(SERVER_BROADCAST_ADDRESSES, "");
    }

    if (!taskParam.isMasterInShellMode) {
      gflags.put(MASTER_ADDRESSES, masterAddresses);
    } else {
      gflags.put(MASTER_ADDRESSES, "");
    }

    gflags.put(
        RPC_BIND_ADDRESSES,
        String.format("%s:%s", privateIp, Integer.toString(node.masterRpcPort)));

    if (useSecondaryIp) {
      String bindAddressPrimary =
          String.format("%s:%s", node.cloudInfo.private_ip, node.masterRpcPort);
      String bindAddressSecondary =
          String.format("%s:%s", node.cloudInfo.secondary_private_ip, node.masterRpcPort);
      String bindAddresses = bindAddressSecondary + "," + bindAddressPrimary;
      gflags.put(RPC_BIND_ADDRESSES, bindAddresses);
    } else if (isDualNet) {
      gflags.put(USE_PRIVATE_IP, "cloud");
    }

    gflags.put(WEBSERVER_PORT, Integer.toString(node.masterHttpPort));
    gflags.put(WEBSERVER_INTERFACE, privateIp);

    return gflags;
  }

  public static boolean shouldSkipServerEndpointVerification(Map<String, String> gflags) {
    return gflags.getOrDefault(VERIFY_SERVER_ENDPOINT_GFLAG, "true").equalsIgnoreCase("false");
  }

  /**
   * Checks consistency between gflags and userIntent. Throws PlatformServiceException if any
   * problems are found.
   *
   * @param userIntent
   */
  public static void checkGflagsAndIntentConsistency(
      UniverseDefinitionTaskParams.UserIntent userIntent) {
    List<Map<String, String>> masterAndTserverGFlags =
        Arrays.asList(userIntent.masterGFlags, userIntent.tserverGFlags);
    if (userIntent.specificGFlags != null) {
      if (userIntent.specificGFlags.isInheritFromPrimary()) {
        return;
      }
      if (userIntent.specificGFlags.getPerProcessFlags() != null) {
        masterAndTserverGFlags =
            Arrays.asList(
                userIntent
                    .specificGFlags
                    .getPerProcessFlags()
                    .value
                    .getOrDefault(UniverseTaskBase.ServerType.MASTER, new HashMap<>()),
                userIntent
                    .specificGFlags
                    .getPerProcessFlags()
                    .value
                    .getOrDefault(UniverseTaskBase.ServerType.TSERVER, new HashMap<>()));
      }
    }
    for (Map<String, String> gflags : masterAndTserverGFlags) {
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
   * Process user gflags: 1) merge some CSV gflags. 2) remove gflags that are forbidden to override
   * (if not allowOverrideAll) 3) check host and port for proxy bind addresses gflags if user
   * specified both.
   *
   * @param node - node details
   * @param userGFlags - glfags specified by user
   * @param platformGFlags - gflags that are generated by platform
   * @param allowOverrideAll - indicates whether we allow user flags to override platform flags
   */
  public static void processUserGFlags(
      NodeDetails node,
      Map<String, String> userGFlags,
      Map<String, String> platformGFlags,
      boolean allowOverrideAll,
      RuntimeConfGetter confGetter,
      AnsibleConfigureServers.Params taskParams) {
    mergeCSVs(userGFlags, platformGFlags, UNDEFOK);
    if (!allowOverrideAll) {
      GFLAGS_FORBIDDEN_TO_OVERRIDE.forEach(
          gflag -> {
            if (userGFlags.containsKey(gflag)
                && platformGFlags.containsKey(gflag)
                && !userGFlags.get(gflag).equals(platformGFlags.get(gflag))) {
              LOG.warn(
                  "Removing {} from user gflags, values mismatch: user {} platform {}",
                  gflag,
                  userGFlags.get(gflag),
                  platformGFlags.get(gflag));
              userGFlags.remove(gflag);
            }
          });
    }

    if (userGFlags.containsKey(PSQL_PROXY_BIND_ADDRESS)) {
      mergeHostAndPort(userGFlags, PSQL_PROXY_BIND_ADDRESS, node.ysqlServerRpcPort);
    }
    if (userGFlags.containsKey(CSQL_PROXY_BIND_ADDRESS)) {
      mergeHostAndPort(userGFlags, CSQL_PROXY_BIND_ADDRESS, node.yqlServerRpcPort);
    }
    if (userGFlags.containsKey(REDIS_PROXY_BIND_ADDRESS)) {
      mergeHostAndPort(userGFlags, REDIS_PROXY_BIND_ADDRESS, node.redisServerRpcPort);
    }
    if (userGFlags.containsKey(YSQL_HBA_CONF_CSV)
        && confGetter.getGlobalConf(GlobalConfKeys.oidcFeatureEnhancements)) {
      /*
       * Preprocess the ysql_hba_conf_csv flag for IdP specific use case.
       * Refer Design Doc:
       * https://docs.google.com/document/d/1SJzZJrAqc0wkXTCuMS7UKi1-5xEuYQKCOOa3QWYpMeM/edit
       */
      processHbaConfFlagIfRequired(node, userGFlags, confGetter, taskParams.getUniverseUUID());
    }
    // Merge the `ysql_hba_conf_csv` post pre-processing the hba conf for jwt if required.
    mergeCSVs(userGFlags, platformGFlags, YSQL_HBA_CONF_CSV);
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

  public static Map<String, String> getBaseGFlags(
      UniverseTaskBase.ServerType serverType,
      UniverseDefinitionTaskParams.Cluster cluster,
      Collection<UniverseDefinitionTaskParams.Cluster> allClusters) {
    return getGFlagsForNode(null, serverType, cluster, allClusters);
  }

  public static Map<String, String> getGFlagsForNode(
      @Nullable NodeDetails node,
      UniverseTaskBase.ServerType serverType,
      UniverseDefinitionTaskParams.Cluster cluster,
      Collection<UniverseDefinitionTaskParams.Cluster> allClusters) {
    UserIntent userIntent = cluster.userIntent;
    UniverseDefinitionTaskParams.Cluster primary =
        allClusters.stream()
            .filter(c -> c.clusterType == UniverseDefinitionTaskParams.ClusterType.PRIMARY)
            .findFirst()
            .orElse(null);
    if (userIntent.specificGFlags != null) {
      if (userIntent.specificGFlags.isInheritFromPrimary()) {
        if (cluster.clusterType == UniverseDefinitionTaskParams.ClusterType.PRIMARY) {
          throw new IllegalStateException("Primary cluster has inherit gflags");
        }
        return getGFlagsForNode(node, serverType, primary, allClusters);
      }
      return userIntent.specificGFlags.getGFlags(node, serverType);
    } else {
      if (cluster.clusterType == UniverseDefinitionTaskParams.ClusterType.ASYNC) {
        return getGFlagsForNode(node, serverType, primary, allClusters);
      }
      return serverType == UniverseTaskBase.ServerType.MASTER
          ? userIntent.masterGFlags
          : userIntent.tserverGFlags;
    }
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

  public static Map<String, String> trimFlags(Map<String, String> data) {
    Map<String, String> trimData = new HashMap<>();
    for (Map.Entry<String, String> intent : data.entrySet()) {
      String key = intent.getKey();
      String value = intent.getValue();
      trimData.put(key.trim(), value.trim());
    }
    return trimData;
  }

  public static void mergeCSVs(
      Map<String, String> userGFlags, Map<String, String> platformGFlags, String key) {
    if (userGFlags.containsKey(key)) {
      String userValue = userGFlags.get(key).toString();
      try {
        CSVFormat csvFormat = CSVFormat.DEFAULT;
        CSVParser userValueParser = new CSVParser(new StringReader(userValue), csvFormat);
        CSVParser platformValuesParser =
            new CSVParser(
                new StringReader(platformGFlags.getOrDefault(key, "").toString()), csvFormat);
        Set<String> records = new LinkedHashSet<>();
        StringWriter writer = new StringWriter();
        CSVPrinter csvPrinter = new CSVPrinter(writer, csvFormat);
        for (CSVRecord record : userValueParser) {
          records.addAll(record.toList());
        }
        for (CSVRecord record : platformValuesParser) {
          records.addAll(record.toList());
        }
        csvPrinter.printRecord(records);
        csvPrinter.flush();
        String result = writer.toString();
        userGFlags.put(key, result.replaceAll("\n", "").replace("\r", ""));
      } catch (IOException ignored) {
        // can't really happen
      }
    }
  }

  private static void mergeHostAndPort(
      Map<String, String> userGFlags, String addressKey, int port) {
    String val = userGFlags.get(addressKey);
    String uriStr = "http://" + val; // adding some arbitrary scheme to parse it as a uri.
    try {
      URI uri = new URI(uriStr);
      if (uri.getPort() != port) {
        LOG.info("Replacing port {} for {} in {}", uri.getPort(), addressKey, val);
        userGFlags.put(addressKey, String.format("%s:%s", uri.getHost(), port));
      }
    } catch (URISyntaxException ex) {
      LOG.warn("Not a uri {}", uriStr);
    }
  }

  public static String updateJwtJWKSPath(
      String hbaConfValue, Path localGflagFilePath, String providerUUID) {
    int jwksIndex = hbaConfValue.indexOf(JWKS_FILE_CONTENT_KEY);
    if (jwksIndex == -1) {
      return hbaConfValue;
    }
    int startIndex = jwksIndex + 5; // Move to the character after "jwks="

    // Find the closing curly brace for the JSON object
    int endIndex = findMatchingClosingBrace(hbaConfValue, startIndex);
    if (endIndex != -1) {
      String jsonNode = hbaConfValue.substring(startIndex, endIndex + 1);
      String fileName = "";
      try {
        fileName = FileUtils.computeHashForAFile(jsonNode, 10);
      } catch (NoSuchAlgorithmException e) {
        LOG.warn("Error generating the hash for a file, {}", e.getMessage());
        // Generate a random string in case of failure.
        fileName = UUID.randomUUID().toString();
      }
      Path localJWKSFilePath = localGflagFilePath.resolve(fileName);
      try {
        Files.write(localJWKSFilePath, jsonNode.getBytes());
      } catch (IOException e) {
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR, String.format("JWKS file write failed %s", e.getMessage()));
      }
      String ybHomeDir = getYbHomeDir(providerUUID);
      String remoteJWKSFilePath = ybHomeDir + GFLAG_REMOTE_FILES_PATH + fileName;
      String jwtJWKSPath = JWT_JWKS_FILE_PATH + "=\"\"" + remoteJWKSFilePath + "\"\"";
      StringBuilder sb = new StringBuilder(hbaConfValue);
      sb.replace(jwksIndex, endIndex + 1, jwtJWKSPath);
      return sb.toString();
    }

    return hbaConfValue;
  }

  public static int findMatchingClosingBrace(String input, int startIndex) {
    // Utility function for forming the complete JSON from a given string.
    // We match the { paranthesis counts to determine the valid Json Object.
    int count = 0;
    for (int i = startIndex; i < input.length(); i++) {
      char c = input.charAt(i);
      if (c == '{') {
        count++;
      } else if (c == '}') {
        count--;
        if (count == 0) {
          return i;
        }
      }
    }
    return -1;
  }

  public static String updateHbaConfValueForJWT(
      String hbaConfValue, Path localGflagFilePath, String providerUUID) {
    List<String> doubleQuotedKeys =
        ImmutableList.of(JWT_AUDIENCES, JWT_ISSUERS, JWT_MATCHING_CLAIM_KEY);
    String updatedHbaConfValue = updateJwtJWKSPath(hbaConfValue, localGflagFilePath, providerUUID);
    for (String key : doubleQuotedKeys) {
      updatedHbaConfValue = processJWTValues(updatedHbaConfValue, key);
    }
    return updatedHbaConfValue;
  }

  public static String processJWTValues(String hbaConfValue, String key) {
    // DB expects the key value to be double double quoted. This utility checks the
    // same & in case they are single quoted wraps them inside double quotes else returns.
    Pattern doubleDoubleQuotePattern = Pattern.compile(key + "\"\".*?\"\"");
    Matcher doubleDoubleQuoteMatcher = doubleDoubleQuotePattern.matcher(hbaConfValue);
    if (doubleDoubleQuoteMatcher.find()) {
      // String contains a double-double-quoted value, return as it is
      return hbaConfValue;
    } else {
      // Wrap the value in double double quotes while handling single quotes
      Pattern pattern = Pattern.compile(key + "\"(.*?)\"");
      Matcher matcher = pattern.matcher(hbaConfValue);
      StringBuffer buffer = new StringBuffer();
      if (matcher.find()) {
        String oldValue = matcher.group(1);
        String replacement =
            Matcher.quoteReplacement(String.format("%s\"\"" + oldValue + "\"\"", key));
        matcher.appendReplacement(buffer, replacement);
      }
      matcher.appendTail(buffer);
      return buffer.toString();
    }
  }

  public static void processHbaConfFlagIfRequired(
      @Nullable NodeDetails node,
      Map<String, String> userFlags,
      RuntimeConfGetter confGetter,
      UUID universeUUID) {
    processHbaConfFlagIfRequired(node, userFlags, confGetter, universeUUID, null);
  }

  public static void processHbaConfFlagIfRequired(
      @Nullable NodeDetails node,
      Map<String, String> userFlags,
      RuntimeConfGetter confGetter,
      UUID universeUUID,
      @Nullable UUID placementUUID) {
    String hbaConfValue = userFlags.get(YSQL_HBA_CONF_CSV);
    Path tmpDirectoryPath =
        FileUtils.getOrCreateTmpDirectory(
            confGetter.getGlobalConf(GlobalConfKeys.ybTmpDirectoryPath));
    Path localGflagFilePath = tmpDirectoryPath;
    if (node != null && node.getNodeUuid() != null) {
      localGflagFilePath = tmpDirectoryPath.resolve(node.getNodeUuid().toString());
    } else if (placementUUID != null) {
      // For k8s universes we will copy the JWKS key to `/tmp/<clusterUUID>`
      localGflagFilePath = tmpDirectoryPath.resolve(placementUUID.toString());
    }
    if (!Files.isDirectory(localGflagFilePath)) {
      try {
        Files.createDirectory(localGflagFilePath);
      } catch (IOException e) {
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR,
            String.format("Failed to create tmp gflag directory, {}", e.getMessage()));
      }
    }
    Universe universe = Universe.getOrBadRequest(universeUUID);
    UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
    if (placementUUID == null) {
      if (node == null || (node != null && node.placementUuid == null)) {
        throw new PlatformServiceException(
            INTERNAL_SERVER_ERROR,
            String.format(
                "Missing placement information for the node in universe {}. Can't Continue",
                universeUUID.toString()));
      } else {
        placementUUID = node.placementUuid;
      }
    }
    UserIntent userIntent = universeDetails.getClusterByUuid(placementUUID).userIntent;
    String providerUUID = userIntent.provider;

    String modifiedHbaConfEntries = "";
    // Split the input string at positions where it starts with "host..." or "local"
    String[] hbaConfEntries = hbaConfValue.split("(?i)(?<=\\s|,|\")\\s*(?=host\\w*|local\\b)");

    for (int i = 0; i < hbaConfEntries.length; i++) {
      String hbaConfEntry = hbaConfEntries[i];
      if (hbaConfEntry.isEmpty()) {
        continue;
      }
      hbaConfEntry.trim();
      if (hbaConfEntry.contains(JWT_AUTH)) {
        StringBuilder modifiedHbaConfEntry = new StringBuilder();
        if (i != 0 && !hbaConfEntries[i - 1].endsWith("\"")) {
          modifiedHbaConfEntry.append("\"");
        }
        modifiedHbaConfEntry.append(
            updateHbaConfValueForJWT(hbaConfEntry, localGflagFilePath, providerUUID));
        if (i != 0 && !hbaConfEntries[i - 1].endsWith("\"")) {
          if (i != hbaConfEntries.length - 1) {
            // Remove the trailing comma
            modifiedHbaConfEntry.setLength(modifiedHbaConfEntry.length() - 1);
          }
          modifiedHbaConfEntry.append("\"");
          if (i != hbaConfEntries.length - 1) {
            // Add the trailing comma
            modifiedHbaConfEntry.append(",");
          }
        }
        modifiedHbaConfEntries += modifiedHbaConfEntry.toString();
      } else {
        modifiedHbaConfEntries += hbaConfEntry;
      }
    }
    userFlags.put(YSQL_HBA_CONF_CSV, modifiedHbaConfEntries);
  }

  /**
   * Checks if provided user gflags have conflicts with default gflags and returns error if true.
   *
   * @param node node for which we are generating default glfags.
   * @param taskParams task params for node.
   * @param userIntent current user intent.
   * @param universe to check.
   * @param userGFlags provider user gflags.
   * @param config
   * @return
   */
  public static String checkForbiddenToOverride(
      NodeDetails node,
      AnsibleConfigureServers.Params taskParams,
      UniverseDefinitionTaskParams.UserIntent userIntent,
      Universe universe,
      Map<String, String> userGFlags,
      Config config) {
    boolean useHostname =
        universe.getUniverseDetails().getPrimaryCluster().userIntent.useHostname
            || !NodeManager.isIpAddress(node.cloudInfo.private_ip);

    Map<String, String> platformGFlags =
        getAllDefaultGFlags(taskParams, universe, userIntent, useHostname, config);
    for (String gflag : GFLAGS_FORBIDDEN_TO_OVERRIDE) {
      if (userGFlags.containsKey(gflag)
          && platformGFlags.containsKey(gflag)
          && !userGFlags.get(gflag).equals(platformGFlags.get(gflag))) {
        return String.format(
            "Node %s: value %s for %s is conflicting with autogenerated value %s",
            node.nodeName, userGFlags.get(gflag), gflag, platformGFlags.get(gflag));
      }
    }
    return null;
  }

  public static void removeGFlag(
      UniverseDefinitionTaskParams.UserIntent userIntent,
      String gflagKey,
      UniverseTaskBase.ServerType... serverTypes) {
    if (userIntent.specificGFlags != null) {
      userIntent.specificGFlags.removeGFlag(gflagKey, serverTypes);
    } else {
      for (UniverseTaskBase.ServerType serverType : serverTypes) {
        switch (serverType) {
          case MASTER:
            userIntent.masterGFlags.remove(gflagKey);
            break;
          case TSERVER:
            userIntent.tserverGFlags.remove(gflagKey);
            break;
        }
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

  public static Set<String> getDeletedGFlags(
      Map<String, String> currentGFlags, Map<String, String> updatedGFlags) {
    return currentGFlags.keySet().stream()
        .filter(flag -> !updatedGFlags.containsKey(flag))
        .collect(Collectors.toSet());
  }

  public static boolean areGflagsInheritedFromPrimary(
      UniverseDefinitionTaskParams.Cluster cluster) {
    if (cluster.clusterType != UniverseDefinitionTaskParams.ClusterType.ASYNC) {
      return false;
    }
    if (cluster.userIntent.specificGFlags == null) {
      return true;
    }
    return cluster.userIntent.specificGFlags.isInheritFromPrimary();
  }
}
