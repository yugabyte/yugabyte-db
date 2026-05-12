// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.NOT_FOUND;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.google.common.collect.ImmutableSet;
import com.google.common.collect.Iterables;
import com.google.common.collect.Sets;
import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.tasks.subtasks.FetchServerConf;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.backuprestore.ybc.YbcManager;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.forms.ImportUniverseTaskParams;
import com.yugabyte.yw.forms.ImportUniverseTaskParams.ServerConfig;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UniverseMigrationConfig;
import com.yugabyte.yw.forms.UniverseMigrationConfig.ServerSpecificConfig;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.CertificateInfo;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.InstanceType.VolumeDetails;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.NodeInstance.State;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Release;
import com.yugabyte.yw.models.ReleaseArtifact;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.PlacementInfo.PlacementAZ;
import com.yugabyte.yw.models.helpers.PlacementInfo.PlacementCloud;
import com.yugabyte.yw.models.helpers.PlacementInfo.PlacementRegion;
import io.ebean.annotation.Transactional;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.function.Function;
import java.util.stream.Collectors;
import java.util.stream.Stream;
import javax.inject.Inject;
import lombok.ToString;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;
import org.apache.commons.collections4.MapUtils;
import org.apache.commons.lang3.StringUtils;
import org.yb.CommonNet.CloudInfoPB;
import org.yb.CommonNet.PlacementBlockPB;
import org.yb.CommonNet.ReplicationInfoPB;
import org.yb.client.YBClient;
import org.yb.master.CatalogEntityInfo.SysClusterConfigEntryPB;
import org.yb.util.ServerInfo;
import play.libs.Json;

/**
 * This task discovers the configurations of an existing OSS cluster and creates universe metadata
 * on YBA. Once the universe metadata is verified manually, migration task must be run to make it
 * YBA compliant.
 */
@Slf4j
@Abortable
public class ImportUniverse extends UniverseTaskBase {
  // Endpoint to get DB version of the cluster.
  private static final String DB_VERSION_ENDPOINT = "http://%s:%s/api/v1/version";

  // Common server gflags that must be the same for all servers of the same type.
  private static final Map<ServerType, Set<String>> COMMON_GFLAGS =
      ImmutableMap.<ServerType, Set<String>>builder()
          .put(
              ServerType.MASTER,
              ImmutableSet.<String>builder()
                  .add(GFlagsUtil.ENABLE_YSQL)
                  .add(GFlagsUtil.YSQL_ENABLE_AUTH)
                  .add(GFlagsUtil.FS_DATA_DIRS)
                  .build())
          .put(
              ServerType.TSERVER,
              ImmutableSet.<String>builder().add(GFlagsUtil.FS_DATA_DIRS).build())
          .build();

  // Reference to the discovered information.
  private final DiscoveredContext context = new DiscoveredContext();

  private final YbcManager ybcManager;

  @Inject
  protected ImportUniverse(BaseTaskDependencies baseTaskDependencies, YbcManager ybcManager) {
    super(baseTaskDependencies);
    this.ybcManager = ybcManager;
  }

  /** Placeholder for discovered information. */
  private static class DiscoveredContext {
    // IP to a map of ServerType and remote information output.
    private final Map<String, Map<ServerType, FetchServerConf.Output>> serverConfs =
        new ConcurrentHashMap<>();
    private final Map<ServerType, InstanceType> serverInstanceTypes = new ConcurrentHashMap<>();
    private Architecture architecture;
    private DBVersionInfo dbVersionInfo;
    private Set<String> masterHosts;
    private Set<String> tserverHosts;
    private String masterLeaderHost;
    private SysClusterConfigEntryPB clusterConfig;

    private Stream<FetchServerConf.Output> getServerConfStream(ServerType serverType) {
      return serverConfs.values().stream().map(m -> m.get(serverType)).filter(Objects::nonNull);
    }

    private FetchServerConf.Output getAnyServerConf(ServerType serverType) {
      return getServerConfStream(serverType).findFirst().orElseThrow();
    }

    private FetchServerConf.Output getServerConf(String host, ServerType serverType) {
      Map<ServerType, FetchServerConf.Output> processConfs = serverConfs.get(host);
      return processConfs == null ? null : processConfs.get(serverType);
    }

    private UniverseMigrationConfig createUniverseMigrationConfig(
        ImportUniverseTaskParams taskParams) {
      UniverseMigrationConfig migrationConfig = new UniverseMigrationConfig();
      ImmutableList.of(ServerType.MASTER, ServerType.TSERVER)
          .forEach(
              serverType -> {
                ServerSpecificConfig specificConfig = new ServerSpecificConfig();
                specificConfig.confFile = getAnyServerConf(serverType).confPath;
                specificConfig.userSystemd = taskParams.userSystemd;
                specificConfig.systemdService =
                    getServiceName(serverType, taskParams.serverConfigs);
                migrationConfig.getServerConfigs().put(serverType, specificConfig);
              });
      return migrationConfig;
    }
  }

  @ToString
  private static class RegionInfo {
    private final Region region;
    private final Map<String, ZoneInfo> zoneInfos;

    RegionInfo(Region region) {
      this.region = region;
      this.zoneInfos = new ConcurrentHashMap<>();
    }
  }

  @ToString
  private static class ZoneInfo {
    private final AvailabilityZone zone;
    private final Set<String> nodeIps;

    ZoneInfo(AvailabilityZone zone) {
      this.zone = zone;
      this.nodeIps = ConcurrentHashMap.newKeySet();
    }
  }

  @JsonIgnoreProperties(ignoreUnknown = true)
  private static class DBVersionInfo {
    @JsonProperty("version_number")
    public String versionNumber;

    @JsonProperty("build_number")
    public String buildNumber;

    @JsonIgnore
    public String getVersion() {
      return String.format("%s-b%s", versionNumber, buildNumber);
    }
  }

  @Override
  protected ImportUniverseTaskParams taskParams() {
    return (ImportUniverseTaskParams) taskParams;
  }

  private ServerConfig getServerConfigOrBadRequest(ServerType serverType) {
    ServerConfig serverConfig = taskParams().serverConfigs.get(serverType);
    if (serverConfig == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Missing server config for " + serverType);
    }
    return serverConfig;
  }

  // This performs the initial discovery of the services.
  private void init() {
    String masterAddrs = String.join(",", taskParams().masterAddrs);
    String certificate = null;
    if (taskParams().certUuid != null) {
      certificate = CertificateInfo.get(taskParams().certUuid).getCertificate();
    }

    try (YBClient client = ybService.getClient(masterAddrs, certificate)) {
      context.masterHosts =
          client.listMasterRaftPeers().getPeersList().stream()
              .map(peerInfo -> peerInfo.getLastKnownPrivateIps().get(0).getHost())
              .collect(Collectors.toSet());
      context.tserverHosts =
          client.listTabletServers().getTabletServersList().stream()
              .map(ServerInfo::getHost)
              .collect(Collectors.toSet());
      context.masterLeaderHost = client.getLeaderMasterHostAndPort().getHost();
      context.clusterConfig = client.getMasterClusterConfig().getConfig();
      log.info("Got master hosts: {}", context.masterHosts);
      log.info("Got tserver hosts: {}", context.tserverHosts);
      log.info("Got master leader host: {}", context.masterLeaderHost);
      log.info("Got cluster config: {}", context.clusterConfig);
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }

  /** Discover information for the cluster. */
  private void discoverData(Provider provider) {
    log.info("Processing server confs");
    Set<String> commonHosts = null;
    // Validate that IPs do not intersect if the nodes are dedicated to a service.
    if (taskParams().dedicatedNodes
        && !(commonHosts = Sets.intersection(context.masterHosts, context.tserverHosts))
            .isEmpty()) {
      log.error("Some dedicated nodes have both master and tservers: {}", commonHosts);
      throw new PlatformServiceException(
          BAD_REQUEST, "Some dedicated nodes have both master and tservers: " + commonHosts);
    }
    Map<String, NodeAgent> nodeAgents =
        NodeAgent.getAll(provider.getCustomerUUID()).stream()
            .collect(Collectors.toMap(NodeAgent::getIp, Function.identity()));
    // Make sure node agents are present for all the discovered IPs.
    if (!nodeAgents.keySet().containsAll(context.serverConfs.keySet())) {
      Set<String> missingIps = Sets.difference(context.serverConfs.keySet(), nodeAgents.keySet());
      log.error("Node agents are missing for {}", missingIps);
      throw new PlatformServiceException(BAD_REQUEST, "Node agents are missing");
    }
    // Validate common confs which must be the same for the server type.
    COMMON_GFLAGS
        .entrySet()
        .forEach(
            e -> {
              ServerType serverType = e.getKey();
              Set<String> commonGflags = e.getValue();
              context
                  .getServerConfStream(serverType)
                  .reduce(
                      (prev, curr) -> {
                        if (!Objects.equals(prev.confPath, curr.confPath)) {
                          log.error(
                              "Some servers have different {} conf path - {} vs {}",
                              serverType,
                              prev.confPath,
                              curr.confPath);
                          throw new PlatformServiceException(
                              BAD_REQUEST,
                              "Some servers have different conf path for " + serverType);
                        }
                        if (!Objects.equals(prev.binaryPath, curr.binaryPath)) {
                          log.error(
                              "Some servers have different {} binary path - {} vs {}",
                              serverType,
                              prev.binaryPath,
                              curr.binaryPath);
                          throw new PlatformServiceException(
                              BAD_REQUEST,
                              "Some servers have different binary path for " + serverType);
                        }
                        if (!commonGflags.stream()
                            .allMatch(
                                f -> Objects.equals(prev.gflags.get(f), curr.gflags.get(f)))) {
                          log.error(
                              "Some servers have different common {} gflags - {} vs {}",
                              serverType,
                              prev.gflags,
                              curr.gflags);
                          throw new PlatformServiceException(
                              BAD_REQUEST,
                              "Some servers have different common gflags for " + serverType);
                        }
                        return curr;
                      });
            });
    if (!taskParams().dedicatedNodes
        && !Objects.equals(
            context.getAnyServerConf(ServerType.MASTER).gflags.get(GFlagsUtil.FS_DATA_DIRS),
            context.getAnyServerConf(ServerType.TSERVER).gflags.get(GFlagsUtil.FS_DATA_DIRS))) {
      // Master and tserver mount points must be the same for non-dedicated nodes.
      throw new PlatformServiceException(
          BAD_REQUEST, "Master and tserver mount points must be the same for non-dedicated nodes");
    }
    // Start further discovery.
    discoverServerArchitecture();
    discoverDBVersion();
    discoverInstanceTypes(provider);
  }

  private void discoverDBVersion() {
    try {
      log.info("Discovering running DB version from the cluster");
      String masterHttpPort =
          context
              .getServerConf(context.masterLeaderHost, ServerType.MASTER)
              .gflags
              .getOrDefault(GFlagsUtil.WEBSERVER_PORT, "7000");
      String endpoint =
          String.format(DB_VERSION_ENDPOINT, context.masterLeaderHost, masterHttpPort);
      JsonNode response = nodeUIApiHelper.getRequest(endpoint);
      JsonNode errors = response.get("error");
      if (errors != null) {
        String errMsg =
            String.format("Error getting cluster config from %s. Error %s", endpoint, errors);
        log.error(errMsg);
        throw new RuntimeException(errMsg);
      }
      context.dbVersionInfo = Json.mapper().treeToValue(response, DBVersionInfo.class);
      String dbVersion = context.dbVersionInfo.getVersion();
      log.info("Got DB version: {}", dbVersion);
      Release release = Release.getByVersion(dbVersion);
      if (release == null) {
        throw new PlatformServiceException(NOT_FOUND, "Non existing DB version " + dbVersion);
      }
      ReleaseArtifact releaseArtifact = release.getArtifactForArchitecture(context.architecture);
      if (releaseArtifact == null) {
        throw new PlatformServiceException(
            NOT_FOUND,
            "Architecture "
                + context.architecture
                + " is not supported by DB version "
                + dbVersion);
      }
    } catch (PlatformServiceException e) {
      throw e;
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }

  private void discoverServerArchitecture() {
    log.info("Discovering server architecture of the nodes");
    NodeAgent nodeAgent =
        NodeAgent.maybeGetByIp(Iterables.get(context.serverConfs.keySet(), 0)).orElseThrow();
    List<String> cmd = Arrays.asList("uname", "-sm");
    ShellResponse response = nodeAgentClient.executeCommand(nodeAgent, cmd).processErrors();
    log.debug("Got output for {}: {}", cmd, response.message);
    if (StringUtils.isBlank(response.message)) {
      throw new RuntimeException("Unknown OS and Arch output: " + response.message);
    }
    // Output is like Linux x86_64.
    String[] parts = response.message.split("\\s+", 2);
    if (parts.length != 2) {
      throw new RuntimeException("Unknown OS and Arch output: " + response.message);
    }
    context.architecture = Architecture.parse(parts[1].trim());
  }

  private void discoverInstanceTypes(Provider provider) {
    log.info("Discovering the mapped instance types for the nodes");
    Map<String, NodeInstance> nodeInstances =
        NodeInstance.listByProvider(provider.getUuid()).stream()
            .collect(Collectors.toMap(i -> i.getDetails().ip, Function.identity()));
    String primaryClusterUuid =
        context
            .clusterConfig
            .getReplicationInfo()
            .getLiveReplicas()
            .getPlacementUuid()
            .toStringUtf8();
    Map<String, InstanceType> instanceTypes = new HashMap<>();
    context
        .serverConfs
        .entrySet()
        .forEach(
            confEntry -> {
              String ip = confEntry.getKey();
              NodeInstance nodeInstance = nodeInstances.get(ip);
              if (nodeInstance == null) {
                log.error("Node instance is not found for {}", ip);
                throw new PlatformServiceException(
                    BAD_REQUEST, "Node instance is not found for " + ip);
              }
              InstanceType instanceType =
                  instanceTypes.computeIfAbsent(
                      nodeInstance.getDetails().instanceType,
                      k ->
                          Iterables.getOnlyElement(
                              InstanceType.getInstanceTypes(
                                  provider.getUuid(), Collections.singleton(k))));
              Map<ServerType, FetchServerConf.Output> serverConfOutputs = confEntry.getValue();
              serverConfOutputs
                  .entrySet()
                  .forEach(
                      outEntry -> {
                        ServerType serverType = outEntry.getKey();
                        FetchServerConf.Output confOutput = outEntry.getValue();
                        InstanceType serverInstanceType =
                            context.serverInstanceTypes.computeIfAbsent(
                                serverType, k -> instanceType);
                        if (!Objects.equals(
                            serverInstanceType.getInstanceTypeCode(),
                            instanceType.getInstanceTypeCode())) {
                          log.error(
                              "Non-homogenous instance type found for {} on {}", serverType, ip);
                          throw new PlatformServiceException(
                              BAD_REQUEST, "Non-homogenous instance type found on " + ip);
                        }
                        String confPlacementRegion =
                            confOutput.gflags.get(GFlagsUtil.PLACEMENT_REGION);
                        if (!Objects.equals(
                            nodeInstance.getDetails().region, confPlacementRegion)) {
                          log.error(
                              "Placement region for {} on {} does not match the region in node"
                                  + " instance",
                              serverType,
                              ip);
                          throw new PlatformServiceException(
                              BAD_REQUEST,
                              "Regions in config and node instance are different for " + ip);
                        }
                        String confPlacementZone = confOutput.gflags.get(GFlagsUtil.PLACEMENT_ZONE);
                        if (!Objects.equals(nodeInstance.getDetails().zone, confPlacementZone)) {
                          log.error(
                              "Placement zone for {} on {} does not match the zone in node"
                                  + " instance",
                              serverType,
                              ip);
                          throw new PlatformServiceException(
                              BAD_REQUEST,
                              "Zones in config and node instance are different for " + ip);
                        }
                        String placementUuid =
                            confOutput.gflags.getOrDefault(GFlagsUtil.PLACEMENT_UUID, "");
                        if (!Objects.equals(primaryClusterUuid, placementUuid)) {
                          log.error(
                              "Only primary cluster is supported. Expected {}, found {}",
                              primaryClusterUuid,
                              placementUuid);
                          throw new PlatformServiceException(
                              BAD_REQUEST,
                              "Only primary cluster is supported. A different placement UUID found"
                                  + " on "
                                  + ip);
                        }
                        String mounthPaths =
                            instanceType.getInstanceTypeDetails().volumeDetailsList.stream()
                                .map(v -> v.mountPath)
                                .collect(Collectors.joining(","));
                        String confMountPaths = confOutput.gflags.get(GFlagsUtil.FS_DATA_DIRS);
                        if (!Objects.equals(mounthPaths, confMountPaths)) {
                          log.error(
                              "Expected mount paths {}, but found for {} in the provider {} ({})",
                              confMountPaths,
                              mounthPaths,
                              provider.getName(),
                              provider.getUuid());
                          throw new PlatformServiceException(
                              BAD_REQUEST,
                              String.format(
                                  "Expected mount paths %s, but found for %s in the provider",
                                  confMountPaths, mounthPaths));
                        }
                      });
            });
  }

  private Map<String, RegionInfo> createRegionInfos(Provider provider) {
    Map<String, RegionInfo> regionInfos = new HashMap<>();
    Map<String, Region> regions =
        provider.getRegions().stream()
            .collect(Collectors.toMap(Region::getCode, Function.identity()));
    context
        .serverConfs
        .entrySet()
        .forEach(
            e -> {
              Map<String, String> gflags = Iterables.getLast(e.getValue().values()).gflags;
              String regionCode = gflags.get(GFlagsUtil.PLACEMENT_REGION);
              String zoneCode = gflags.get(GFlagsUtil.PLACEMENT_ZONE);
              RegionInfo regionInfo =
                  regionInfos.computeIfAbsent(
                      regionCode, k -> new RegionInfo(regions.get(regionCode)));
              ZoneInfo zoneInfo =
                  regionInfo.zoneInfos.computeIfAbsent(
                      zoneCode,
                      k -> {
                        Map<String, AvailabilityZone> zones =
                            AvailabilityZone.getAZsForRegion(regionInfo.region.getUuid()).stream()
                                .collect(
                                    Collectors.toMap(
                                        AvailabilityZone::getCode, Function.identity()));
                        return new ZoneInfo(zones.get(zoneCode));
                      });
              zoneInfo.nodeIps.add(e.getKey());
            });
    return regionInfos;
  }

  public PlacementInfo createPlacementInfo(
      Provider provider, UserIntent userIntent, Map<String, RegionInfo> regionInfos) {
    PlacementInfo placementInfo = new PlacementInfo();
    ReplicationInfoPB replicationInfo = context.clusterConfig.getReplicationInfo();
    List<PlacementBlockPB> placementBlocks =
        replicationInfo.getLiveReplicas().getPlacementBlocksList();
    if (CollectionUtils.isEmpty(placementBlocks)) {
      // Default placement info.
      List<ZoneInfo> zoneInfos =
          regionInfos.values().stream()
              .flatMap(r -> r.zoneInfos.values().stream())
              .collect(Collectors.toList());
      for (int i = 0; i < Math.min(zoneInfos.size(), userIntent.replicationFactor); i++) {
        ZoneInfo zoneInfo = zoneInfos.get(i);
        PlacementInfoUtil.addPlacementZone(zoneInfo.zone.getUuid(), placementInfo);
      }
      return placementInfo;
    }
    PlacementCloud placementCloud = new PlacementInfo.PlacementCloud();
    placementCloud.uuid = provider.getUuid();
    placementCloud.code = provider.getCode();
    placementCloud.regionList = new ArrayList<>();
    Map<String, List<PlacementBlockPB>> regionPlacementBlocks =
        placementBlocks.stream()
            .collect(Collectors.groupingBy(b -> b.getCloudInfo().getPlacementRegion()));

    Map<String, String> affinitizedAzs = new HashMap<>();
    List<CloudInfoPB> affinitizedLeaders = replicationInfo.getAffinitizedLeadersList();
    if (CollectionUtils.isNotEmpty(affinitizedLeaders)) {
      affinitizedLeaders.forEach(
          c -> affinitizedAzs.put(c.getPlacementRegion(), c.getPlacementZone()));
    }
    regionPlacementBlocks
        .entrySet()
        .forEach(
            e -> {
              RegionInfo regionInfo = regionInfos.get(e.getKey());
              List<PlacementBlockPB> azPlacementBlocks = e.getValue();
              PlacementRegion placementRegion = new PlacementRegion();
              placementRegion.uuid = regionInfo.region.getUuid();
              placementRegion.name = regionInfo.region.getName();
              placementRegion.code = regionInfo.region.getCode();
              placementRegion.azList = new ArrayList<>();
              azPlacementBlocks.forEach(
                  b -> {
                    ZoneInfo zoneInfo =
                        regionInfo.zoneInfos.get(b.getCloudInfo().getPlacementZone());
                    PlacementAZ placementAz = new PlacementAZ();
                    placementAz.name = zoneInfo.zone.getName();
                    placementAz.subnet = zoneInfo.zone.getSubnet();
                    placementAz.replicationFactor = b.getMinNumReplicas();
                    placementAz.uuid = zoneInfo.zone.getUuid();
                    placementAz.numNodesInAZ = b.getMinNumReplicas();
                    placementAz.isAffinitized =
                        Objects.equals(
                            affinitizedAzs.get(e.getKey()), b.getCloudInfo().getPlacementZone());
                    placementRegion.azList.add(placementAz);
                  });
              placementCloud.regionList.add(placementRegion);
            });
    placementInfo.cloudList.add(placementCloud);
    return placementInfo;
  }

  private DeviceInfo createDeviceInfo(Provider provider, ServerType serverType) {
    String confMountPaths =
        context.getAnyServerConf(serverType).gflags.get(GFlagsUtil.FS_DATA_DIRS);
    InstanceType instanceType = context.serverInstanceTypes.get(serverType);
    List<VolumeDetails> volumeDetailsList = instanceType.getInstanceTypeDetails().volumeDetailsList;
    DeviceInfo deviceInfo = new DeviceInfo();
    deviceInfo.numVolumes = volumeDetailsList.size();
    deviceInfo.mountPoints = confMountPaths;
    deviceInfo.storageClass = "standard";
    deviceInfo.volumeSize = volumeDetailsList.get(0).volumeSizeGB;
    return deviceInfo;
  }

  private ServerType getServerType(String ip) {
    if (context.masterHosts.contains(ip) && context.tserverHosts.contains(ip)) {
      return ServerType.EITHER;
    }
    return context.masterHosts.contains(ip) ? ServerType.MASTER : ServerType.TSERVER;
  }

  private UserIntent createUserIntent(Provider provider, Map<String, RegionInfo> regionInfos) {
    String dbVersion = context.dbVersionInfo.getVersion();
    FetchServerConf.Output masterConfOutput = context.getAnyServerConf(ServerType.MASTER);
    FetchServerConf.Output tserverConfOutput = context.getAnyServerConf(ServerType.TSERVER);
    log.info("Got any master conf: {}", masterConfOutput);
    log.info("Got any tserver conf: {}", tserverConfOutput);
    ServerConfig masterServerConfig = getServerConfigOrBadRequest(ServerType.MASTER);
    ServerConfig tserverServerConfig = getServerConfigOrBadRequest(ServerType.TSERVER);
    // Create and prepare the user intent.
    UserIntent userIntent = new UserIntent();
    userIntent.universeName = taskParams().universeName;
    userIntent.ybSoftwareVersion = dbVersion;
    userIntent.providerType = provider.getCloudCode();
    userIntent.provider = provider.getUuid().toString();
    userIntent.useSystemd = true;
    userIntent.instanceType =
        context.serverInstanceTypes.get(ServerType.TSERVER).getInstanceTypeCode();
    userIntent.replicationFactor =
        Integer.parseInt(masterConfOutput.gflags.get("replication_factor"));
    userIntent.dedicatedNodes = taskParams().dedicatedNodes;
    userIntent.instanceTags = taskParams().instanceTags;
    userIntent.numNodes = Sets.union(context.masterHosts, context.tserverHosts).size();
    userIntent.enableExposingService = UniverseDefinitionTaskParams.ExposingServiceState.UNEXPOSED;
    userIntent.deviceInfo = createDeviceInfo(provider, ServerType.TSERVER);
    if (taskParams().dedicatedNodes) {
      userIntent.masterDeviceInfo = createDeviceInfo(provider, ServerType.MASTER);
      userIntent.masterInstanceType =
          context.serverInstanceTypes.get(ServerType.MASTER).getInstanceTypeCode();
    }
    // Record the migration config to be used later in migration.
    userIntent.setMigrationConfig(context.createUniverseMigrationConfig(taskParams()));
    Map<String, String> masterSpecificGflags =
        MapUtils.isEmpty(masterServerConfig.specificGflags)
            ? new HashMap<>()
            : masterServerConfig.specificGflags;
    Map<String, String> tserverSpecificGflags =
        MapUtils.isEmpty(tserverServerConfig.specificGflags)
            ? new HashMap<>()
            : tserverServerConfig.specificGflags;

    userIntent.specificGFlags =
        SpecificGFlags.construct(masterSpecificGflags, tserverSpecificGflags);
    userIntent.regionList =
        regionInfos.values().stream().map(i -> i.region.getUuid()).collect(Collectors.toList());
    // Sync the gflags to user intent.
    GFlagsUtil.syncGflagsToIntent(masterConfOutput.gflags, userIntent);
    GFlagsUtil.syncGflagsToIntent(tserverConfOutput.gflags, userIntent);
    return userIntent;
  }

  private UniverseDefinitionTaskParams createUniverseDetails(
      Provider provider, UserIntent userIntent) {
    UniverseDefinitionTaskParams universeDetails = new UniverseDefinitionTaskParams();
    universeDetails.setUniverseUUID(taskParams().universeUuid);
    if (Util.compareYbVersions(
            userIntent.ybSoftwareVersion,
            confGetter.getGlobalConf(GlobalConfKeys.ybcCompatibleDbVersion),
            true)
        < 0) {
      universeDetails.setEnableYbc(false);
      log.warn(
          "Ybc installation is skipped on VM universe with DB version lower than "
              + confGetter.getGlobalConf(GlobalConfKeys.ybcCompatibleDbVersion));
    } else {
      universeDetails.setEnableYbc(true);
      universeDetails.setYbcSoftwareVersion(ybcManager.getStableYbcVersion());
    }
    universeDetails.setEnableYbc(true);
    universeDetails.expectedUniverseVersion = -1;
    universeDetails.nodePrefix = taskParams().universeName;
    universeDetails.arch = context.architecture;
    universeDetails.platformUrl = taskParams().platformUrl;
    universeDetails.extraDependencies.installNodeExporter =
        provider.getDetails().installNodeExporter;
    return universeDetails;
  }

  private Set<NodeDetails> createNodeDetails(
      Provider provider, Cluster cluster, Map<String, RegionInfo> regionInfos) {
    Set<NodeDetails> nodes = new HashSet<>(context.serverConfs.size());
    AtomicInteger nodeIdx = new AtomicInteger(0);
    Map<String, NodeInstance> nodeInstances =
        NodeInstance.listByProvider(provider.getUuid()).stream()
            .collect(Collectors.toMap(n -> n.getDetails().ip, Function.identity()));
    regionInfos
        .values()
        .forEach(
            r -> {
              r.zoneInfos
                  .values()
                  .forEach(
                      z -> {
                        z.nodeIps.stream()
                            .forEach(
                                ip -> {
                                  // This node IP is bound to a specific region and zone.
                                  NodeInstance nodeInstance = nodeInstances.get(ip);
                                  // Revalidate it.
                                  if (!r.region
                                      .getCode()
                                      .equals(nodeInstance.getDetails().region)) {
                                    log.error(
                                        "Expected region {}, but found region {} for node {}",
                                        r.region,
                                        nodeInstance.getDetails().region,
                                        ip);
                                    throw new PlatformServiceException(
                                        BAD_REQUEST, "Mismatched region detected for node " + ip);
                                  }
                                  if (!z.zone.getCode().equals(nodeInstance.getDetails().zone)) {
                                    log.error(
                                        "Expected zone {}, but found zone {} for node {}",
                                        r.region,
                                        nodeInstance.getDetails().region,
                                        ip);
                                    throw new PlatformServiceException(
                                        BAD_REQUEST, "Mismatched zone detected for node " + ip);
                                  }
                                  if (nodeInstance.isUsed()) {
                                    log.error("Node {} is already being used", ip);
                                    throw new PlatformServiceException(
                                        BAD_REQUEST, "Node " + ip + " is already being used");
                                  }
                                  NodeDetails node = new NodeDetails();
                                  node.migrationPending = true;
                                  node.nodeIdx = nodeIdx.incrementAndGet();
                                  node.cloudInfo = new CloudSpecificInfo();
                                  node.cloudInfo.private_ip = ip;
                                  node.cloudInfo.public_ip = ip;
                                  node.cloudInfo.az = z.zone.getCode();
                                  node.cloudInfo.region = r.region.getCode();
                                  node.cloudInfo.cloud = provider.getCloudCode().toString();
                                  node.cloudInfo.instance_type = nodeInstance.getInstanceTypeCode();
                                  node.cloudInfo.mount_roots =
                                      cluster.userIntent.deviceInfo.mountPoints;
                                  node.placementUuid = cluster.uuid;
                                  node.azUuid = z.zone.getUuid();
                                  node.state = NodeState.Live;
                                  node.nodeUuid = nodeInstance.getNodeUuid();
                                  node.disksAreMountedByUUID = true;
                                  node.cronsActive = false;
                                  node.isMaster = false;
                                  node.isTserver = false;
                                  ServerType serverType = getServerType(ip);
                                  if (serverType == ServerType.EITHER) {
                                    node.isMaster = true;
                                    node.isTserver = true;
                                  } else if (serverType == ServerType.MASTER) {
                                    node.dedicatedTo = ServerType.MASTER;
                                    // This will get corrected on migration.
                                    node.cloudInfo.mount_roots =
                                        cluster.userIntent.masterDeviceInfo.mountPoints;
                                    node.isMaster = true;
                                  } else {
                                    node.dedicatedTo = ServerType.TSERVER;
                                    node.isTserver = true;
                                  }
                                  node.nodeName =
                                      UniverseDefinitionTaskBase.getNodeName(
                                          cluster,
                                          "" /*nameTagValue*/,
                                          taskParams().universeName,
                                          node.nodeIdx,
                                          node.cloudInfo.region,
                                          node.cloudInfo.az);
                                  // Properties inherited from user intent.
                                  node.isYsqlServer = cluster.userIntent.enableYSQL;
                                  node.isYqlServer = cluster.userIntent.enableYCQL;
                                  node.isRedisServer = cluster.userIntent.enableYEDIS;
                                  log.info("Got node: {}", node);
                                  nodes.add(node);
                                });
                      });
            });
    return nodes;
  }

  @Transactional
  private void createUniverse(
      Customer customer, UniverseDefinitionTaskParams universeDetails, Set<NodeDetails> nodes) {
    Cluster cluster = universeDetails.getPrimaryCluster();
    nodes.forEach(
        n -> {
          NodeInstance nodeInstance = NodeInstance.getOrBadRequest(n.getNodeUuid());
          nodeInstance.setNodeName(n.getNodeName());
          nodeInstance.setInUse(true);
          nodeInstance.setState(State.USED);
          nodeInstance.update();
        });
    universeDetails.nodeDetailsSet = nodes;
    Universe universe = Universe.create(universeDetails, customer.getId());
    if (cluster.userIntent.regionList.size() > 1) {
      universe.updateConfig(ImmutableMap.of(Universe.IS_MULTIREGION, Boolean.TRUE.toString()));
    }
  }

  private static String getServiceName(
      ServerType serverType, Map<ServerType, ServerConfig> serverConfigs) {
    String serviceName = null;
    ServerConfig serverConfig = null;
    if (serverConfigs != null && (serverConfig = serverConfigs.get(serverType)) != null) {
      serviceName = serverConfig.serviceName;
    }
    if (StringUtils.isEmpty(serviceName)) {
      serviceName = "yb-" + serverType.name().toLowerCase();
    }
    return serviceName;
  }

  @Override
  public void validateParams(boolean isFirstTry) {
    if (!Util.isValidUniverseNameFormat(taskParams().universeName)) {
      throw new PlatformServiceException(BAD_REQUEST, Util.UNIVERSE_NAME_ERROR_MESG);
    }
  }

  @Override
  public void run() {
    log.debug("Started import universe task");
    Customer customer = Customer.getOrBadRequest(taskParams().customerUuid);
    Provider provider = Provider.getOrBadRequest(taskParams().providerUuid);
    init();
    createFetchServerConfTasks(
        context.masterHosts,
        ServerType.MASTER,
        getServiceName(ServerType.MASTER, taskParams().serverConfigs),
        taskParams().userSystemd,
        o ->
            context
                .serverConfs
                .computeIfAbsent(o.ip, k -> new ConcurrentHashMap<>())
                .put(ServerType.MASTER, o));
    createFetchServerConfTasks(
        context.tserverHosts,
        ServerType.TSERVER,
        getServiceName(ServerType.TSERVER, taskParams().serverConfigs),
        taskParams().userSystemd,
        o ->
            context
                .serverConfs
                .computeIfAbsent(o.ip, k -> new ConcurrentHashMap<>())
                .put(ServerType.TSERVER, o));

    // Run the subtasks and wait.
    getRunnableTask().runSubTasks();
    log.info("Got masters: {}", context.masterHosts);
    log.info("Got tservers: {}", context.tserverHosts);
    // Discover data after the hosts are known.
    discoverData(provider);
    // Create the region info from the cluster config.
    Map<String, RegionInfo> regionInfos = createRegionInfos(provider);
    log.info("Got region info: {}", regionInfos);
    UserIntent userIntent = createUserIntent(provider, regionInfos);
    log.info("Got user intent: {}", userIntent);
    // Create the placement info.
    PlacementInfo placementInfo = createPlacementInfo(provider, userIntent, regionInfos);
    log.info("Got placement info: {}", placementInfo);

    UniverseDefinitionTaskParams universeDetails = createUniverseDetails(provider, userIntent);
    String placementUuid =
        context
            .clusterConfig
            .getReplicationInfo()
            .getLiveReplicas()
            .getPlacementUuid()
            .toStringUtf8();
    UUID clusterUuid =
        StringUtils.isNotBlank(placementUuid) ? UUID.fromString(placementUuid) : UUID.randomUUID();
    Cluster cluster =
        universeDetails.upsertCluster(
            userIntent, null, placementInfo, clusterUuid, ClusterType.PRIMARY);
    Set<NodeDetails> nodes = createNodeDetails(provider, cluster, regionInfos);
    createUniverse(customer, universeDetails, nodes);
  }
}
