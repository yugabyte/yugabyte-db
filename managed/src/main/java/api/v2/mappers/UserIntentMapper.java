// Copyright (c) YugaByte, Inc.
package api.v2.mappers;

import api.v2.models.AvailabilityZoneGFlags;
import api.v2.models.AvailabilityZoneNetworking;
import api.v2.models.AvailabilityZoneNodeSpec;
import api.v2.models.ClusterAddSpec;
import api.v2.models.ClusterEditSpec;
import api.v2.models.ClusterGFlags;
import api.v2.models.ClusterNetworkingSpec;
import api.v2.models.ClusterNetworkingSpec.EnableExposingServiceEnum;
import api.v2.models.ClusterNodeSpec;
import api.v2.models.ClusterProviderEditSpec;
import api.v2.models.ClusterProviderSpec;
import api.v2.models.ClusterSpec;
import api.v2.models.ClusterSpec.ClusterTypeEnum;
import api.v2.models.ClusterStorageSpec;
import api.v2.models.ClusterStorageSpec.StorageTypeEnum;
import api.v2.models.NodeProxyConfig;
import api.v2.models.PerProcessNodeSpec;
import com.yugabyte.yw.cloud.PublicCloudConstants.StorageType;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.common.gflags.SpecificGFlags.PerProcessFlags;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.AZOverrides;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ExposingServiceState;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.PerProcessDetails;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent.K8SNodeResourceSpec;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntentOverrides;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.ProxyConfig;
import com.yugabyte.yw.models.helpers.audit.AuditLogConfig;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Map.Entry;
import java.util.UUID;
import org.mapstruct.InheritInverseConfiguration;
import org.mapstruct.Mapper;
import org.mapstruct.Mapping;
import org.mapstruct.MappingTarget;
import org.mapstruct.ValueMapping;
import org.mapstruct.ValueMappings;

@Mapper(config = CentralConfig.class)
public interface UserIntentMapper {

  default ClusterNodeSpec userIntentToClusterNodeSpec(
      UniverseDefinitionTaskParams.UserIntent userIntent) {
    if (userIntent == null) {
      return null;
    }
    ClusterNodeSpec clusterNodeSpec = new ClusterNodeSpec();

    // top-level node spec from UserIntent
    clusterNodeSpec.setInstanceType(userIntent.instanceType);
    clusterNodeSpec.setStorageSpec(deviceInfoToStorageSpec(userIntent.deviceInfo));
    clusterNodeSpec.setCgroupSize(userIntent.getCgroupSize());
    clusterNodeSpec.setK8sMasterResourceSpec(
        toV2K8SNodeResourceSpec(userIntent.masterK8SNodeResourceSpec));
    clusterNodeSpec.setK8sTserverResourceSpec(
        toV2K8SNodeResourceSpec(userIntent.tserverK8SNodeResourceSpec));

    // tserver/master node spec from UserIntentOverrides
    UserIntentOverrides overrides = userIntent.getUserIntentOverrides();
    if (overrides != null && overrides.getPerProcess() != null) {
      if (overrides.getPerProcess().get(ServerType.TSERVER) != null) {
        PerProcessNodeSpec tserverNodeSpec = new PerProcessNodeSpec();
        tserverNodeSpec.setInstanceType(userIntent.instanceType);
        tserverNodeSpec.setStorageSpec(deviceInfoToStorageSpec(userIntent.deviceInfo));
        clusterNodeSpec.setTserver(tserverNodeSpec);
      }
      if (overrides.getPerProcess().get(ServerType.MASTER) != null) {
        PerProcessNodeSpec masterNodeSpec = new PerProcessNodeSpec();
        masterNodeSpec.setInstanceType(userIntent.instanceType);
        masterNodeSpec.setStorageSpec(deviceInfoToStorageSpec(userIntent.deviceInfo));
        clusterNodeSpec.setMaster(masterNodeSpec);
      }
    }

    // az node spec from UserIntent
    if (overrides != null && overrides.getAzOverrides() != null) {
      Map<String, AvailabilityZoneNodeSpec> azNodeSpec = new HashMap<>();
      overrides
          .getAzOverrides()
          .forEach(
              (azUuid, azOverrides) -> {
                AvailabilityZoneNodeSpec azNode = new AvailabilityZoneNodeSpec();
                azNode.setInstanceType(azOverrides.getInstanceType());
                azNode.setStorageSpec(deviceInfoToStorageSpec(azOverrides.getDeviceInfo()));
                azNode.setCgroupSize(azOverrides.getCgroupSize());
                if (azOverrides.getPerProcess() != null) {
                  PerProcessDetails tserverOverrides =
                      azOverrides.getPerProcess().get(ServerType.TSERVER);
                  if (tserverOverrides != null) {
                    PerProcessNodeSpec azTserver = new PerProcessNodeSpec();
                    azTserver.setInstanceType(tserverOverrides.getInstanceType());
                    azTserver.setStorageSpec(
                        deviceInfoToStorageSpec(tserverOverrides.getDeviceInfo()));
                    azNode.setTserver(azTserver);
                  }
                  PerProcessDetails masterOverrides =
                      azOverrides.getPerProcess().get(ServerType.MASTER);
                  if (masterOverrides != null) {
                    PerProcessNodeSpec azMaster = new PerProcessNodeSpec();
                    azMaster.setInstanceType(masterOverrides.getInstanceType());
                    azMaster.setStorageSpec(
                        deviceInfoToStorageSpec(masterOverrides.getDeviceInfo()));
                    azNode.setMaster(azMaster);
                  }
                }
                azNodeSpec.put(azUuid.toString(), azNode);
              });
      clusterNodeSpec.setAzNodeSpec(azNodeSpec);
    }
    return clusterNodeSpec;
  }

  ClusterStorageSpec deviceInfoToStorageSpec(DeviceInfo deviceInfo);

  @ValueMappings({
    @ValueMapping(target = "SCRATCH", source = "Scratch"),
    @ValueMapping(target = "PERSISTENT", source = "Persistent"),
    @ValueMapping(target = "STANDARDSSD_LRS", source = "StandardSSD_LRS"),
    @ValueMapping(target = "PREMIUM_LRS", source = "Premium_LRS"),
    @ValueMapping(target = "ULTRASSD_LRS", source = "UltraSSD_LRS"),
    @ValueMapping(target = "LOCAL", source = "Local"),
  })
  StorageTypeEnum mapStorageType(StorageType storageType);

  api.v2.models.K8SNodeResourceSpec toV2K8SNodeResourceSpec(
      K8SNodeResourceSpec v1K8SNodeResourceSpec);

  // @Mapping(target = "enableLb", source = "enableLB")
  default ClusterNetworkingSpec userIntentToClusterNetworkingSpec(
      UniverseDefinitionTaskParams.UserIntent userIntent) {
    if (userIntent == null) {
      return null;
    }
    ClusterNetworkingSpec clusterNetworkingSpec = new ClusterNetworkingSpec();
    clusterNetworkingSpec.setEnableLb(userIntent.enableLB);
    clusterNetworkingSpec.setEnableExposingService(
        toV2EnableExposingServiceEnum(userIntent.enableExposingService));
    clusterNetworkingSpec.setProxyConfig(toV2ProxyConfig(userIntent.getProxyConfig()));
    // per az
    if (userIntent.getUserIntentOverrides() != null
        && userIntent.getUserIntentOverrides().getAZProxyConfigMap() != null) {
      Map<String, AvailabilityZoneNetworking> azNetworking = new HashMap<>();
      userIntent
          .getUserIntentOverrides()
          .getAZProxyConfigMap()
          .forEach(
              (azUuid, azProxyConfig) -> {
                AvailabilityZoneNetworking azNetworkingItem = new AvailabilityZoneNetworking();
                azNetworkingItem.setProxyConfig(toV2ProxyConfig(azProxyConfig));
                azNetworking.put(azUuid.toString(), azNetworkingItem);
              });
      clusterNetworkingSpec.setAzNetworking(azNetworking);
    }
    return clusterNetworkingSpec;
  }

  NodeProxyConfig toV2ProxyConfig(ProxyConfig v1ProxyConfig);

  EnableExposingServiceEnum toV2EnableExposingServiceEnum(
      ExposingServiceState v1ExposingServiceState);

  default ClusterGFlags specificGFlagsToClusterGFlags(
      UniverseDefinitionTaskParams.UserIntent userIntent) {
    if (userIntent == null) {
      return null;
    }
    SpecificGFlags specificGFlags = userIntent.specificGFlags;
    if (specificGFlags == null) {
      return null;
    }
    ClusterGFlags clusterGFlags = new ClusterGFlags();
    if (specificGFlags.getPerProcessFlags() != null) {
      clusterGFlags.master(specificGFlags.getPerProcessFlags().value.get(ServerType.MASTER));
      clusterGFlags.tserver(specificGFlags.getPerProcessFlags().value.get(ServerType.TSERVER));
    }
    if (specificGFlags.getPerAZ() != null) {
      for (Entry<UUID, PerProcessFlags> entry : specificGFlags.getPerAZ().entrySet()) {
        AvailabilityZoneGFlags azGFlags = new AvailabilityZoneGFlags();
        azGFlags.setMaster(entry.getValue().value.get(ServerType.MASTER));
        azGFlags.setTserver(entry.getValue().value.get(ServerType.TSERVER));
        clusterGFlags.putAzGflagsItem(entry.getKey().toString(), azGFlags);
      }
    }
    return clusterGFlags;
  }

  @Mapping(target = "awsInstanceProfile", source = "awsArnString")
  @Mapping(target = "imageBundleUuid", source = "imageBundleUUID")
  @Mapping(target = "helmOverrides", source = "universeOverrides")
  @Mapping(target = "azHelmOverrides", source = "azOverrides")
  @Mapping(target = "provider", expression = "java(UUID.fromString(userIntent.provider))")
  ClusterProviderSpec userIntentToClusterProviderSpec(UserIntent userIntent);

  // v2 to v1 mappings begin here

  default UserIntent toV1UserIntent(ClusterSpec clusterSpec) {
    if (clusterSpec == null) {
      return null;
    }

    UniverseDefinitionTaskParams.UserIntent userIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    if (clusterSpec.getNumNodes() != null) {
      userIntent.numNodes = clusterSpec.getNumNodes();
    }
    if (clusterSpec.getReplicationFactor() != null) {
      userIntent.replicationFactor = clusterSpec.getReplicationFactor();
    }
    if (clusterSpec.getDedicatedNodes() != null) {
      userIntent.dedicatedNodes = clusterSpec.getDedicatedNodes();
    }
    fillUserIntentFromClusterNodeSpec(clusterSpec.getNodeSpec(), userIntent);
    fillUserIntentFromClusterNetworkingSpec(clusterSpec.getNetworkingSpec(), userIntent);
    fillUserIntentFromClusterProviderSpec(clusterSpec.getProviderSpec(), userIntent);

    if (clusterSpec.getUseSpotInstance() != null) {
      userIntent.useSpotInstance = clusterSpec.getUseSpotInstance();
    }
    Map<String, String> instanceTags = clusterSpec.getInstanceTags();
    if (instanceTags != null) {
      userIntent.instanceTags = new LinkedHashMap<String, String>(instanceTags);
    }
    userIntent.auditLogConfig = toV1AuditLogConfig(clusterSpec.getAuditLogConfig());
    userIntent.specificGFlags = clusterSpecToSpecificGFlags(clusterSpec);

    return userIntent;
  }

  default UserIntent toV1UserIntentFromClusterEditSpec(
      ClusterEditSpec clusterEditSpec, @MappingTarget UserIntent userIntent) {
    if (clusterEditSpec == null) {
      return userIntent;
    }
    if (userIntent == null) {
      userIntent = new UniverseDefinitionTaskParams.UserIntent();
    }
    if (clusterEditSpec.getNumNodes() != null) {
      userIntent.numNodes = clusterEditSpec.getNumNodes();
    }
    fillUserIntentFromClusterNodeSpec(clusterEditSpec.getNodeSpec(), userIntent);
    fillUserIntentFromClusterProviderEditSpec(clusterEditSpec.getProviderSpec(), userIntent);
    Map<String, String> instanceTags = clusterEditSpec.getInstanceTags();
    if (instanceTags != null) {
      userIntent.instanceTags = new LinkedHashMap<String, String>(instanceTags);
    }
    return userIntent;
  }

  default UserIntent overwriteUserIntentFromClusterAddSpec(
      ClusterAddSpec clusterAddSpec, @MappingTarget UserIntent userIntent) {
    if (clusterAddSpec == null) {
      return userIntent;
    }
    if (clusterAddSpec.getNumNodes() != null) {
      userIntent.numNodes = clusterAddSpec.getNumNodes();
    }
    fillUserIntentFromClusterNodeSpec(clusterAddSpec.getNodeSpec(), userIntent);
    fillUserIntentFromClusterProviderEditSpec(clusterAddSpec.getProviderSpec(), userIntent);

    Map<String, String> instanceTags = clusterAddSpec.getInstanceTags();
    if (instanceTags != null) {
      if (userIntent.instanceTags == null) {
        userIntent.instanceTags = new LinkedHashMap<String, String>(instanceTags);
      } else {
        userIntent.instanceTags.clear();
        userIntent.instanceTags.putAll(instanceTags);
      }
    }
    if (clusterAddSpec != null) {
      userIntent.specificGFlags = clusterAddSpecToSpecificGFlags(clusterAddSpec);
    }
    return userIntent;
  }

  @InheritInverseConfiguration
  StorageType mapStorageTypeEnum(StorageTypeEnum storageType);

  default UserIntent fillUserIntentFromClusterNodeSpec(
      ClusterNodeSpec clusterNodeSpec, UserIntent userIntent) {
    if (clusterNodeSpec == null) {
      return userIntent;
    }
    userIntent.instanceType = clusterNodeSpec.getInstanceType();
    userIntent.deviceInfo = storageSpecToDeviceInfo(clusterNodeSpec.getStorageSpec());
    userIntent.setCgroupSize(clusterNodeSpec.getCgroupSize());
    userIntent.masterK8SNodeResourceSpec =
        toV1K8SNodeResourceSpec(clusterNodeSpec.getK8sMasterResourceSpec());
    userIntent.tserverK8SNodeResourceSpec =
        toV1K8SNodeResourceSpec(clusterNodeSpec.getK8sTserverResourceSpec());

    // fill user intent overrides
    if (clusterNodeSpec.getMaster() != null
        && (clusterNodeSpec.getMaster().getInstanceType() != null
            || clusterNodeSpec.getMaster().getStorageSpec() != null)) {
      UserIntentOverrides overrides = userIntent.getUserIntentOverrides();
      if (overrides == null) {
        overrides = new UserIntentOverrides();
        userIntent.setUserIntentOverrides(overrides);
      }
      PerProcessDetails masterOverrides = new PerProcessDetails();
      masterOverrides.setInstanceType(clusterNodeSpec.getMaster().getInstanceType());
      masterOverrides.setDeviceInfo(
          storageSpecToDeviceInfo(clusterNodeSpec.getMaster().getStorageSpec()));
      Map<ServerType, PerProcessDetails> perProcess = overrides.getPerProcess();
      if (perProcess == null) {
        perProcess = new HashMap<>();
        overrides.setPerProcess(perProcess);
      }
      perProcess.put(ServerType.MASTER, masterOverrides);
    }
    if (clusterNodeSpec.getTserver() != null) {
      UserIntentOverrides overrides = userIntent.getUserIntentOverrides();
      if (overrides == null) {
        overrides = new UserIntentOverrides();
        userIntent.setUserIntentOverrides(overrides);
      }
      PerProcessDetails tserverOverrides = new PerProcessDetails();
      tserverOverrides.setInstanceType(clusterNodeSpec.getTserver().getInstanceType());
      tserverOverrides.setDeviceInfo(
          storageSpecToDeviceInfo(clusterNodeSpec.getTserver().getStorageSpec()));
      Map<ServerType, PerProcessDetails> perProcess = overrides.getPerProcess();
      if (perProcess == null) {
        perProcess = new HashMap<>();
        overrides.setPerProcess(perProcess);
      }
      perProcess.put(ServerType.TSERVER, tserverOverrides);
    }
    if (clusterNodeSpec.getAzNodeSpec() != null) {
      clusterNodeSpec
          .getAzNodeSpec()
          .forEach(
              (azUuid, azNode) -> {
                UserIntentOverrides overrides = userIntent.getUserIntentOverrides();
                if (overrides == null) {
                  overrides = new UserIntentOverrides();
                  userIntent.setUserIntentOverrides(overrides);
                }
                AZOverrides azOverrides = new AZOverrides();
                azOverrides.setInstanceType(azNode.getInstanceType());
                azOverrides.setDeviceInfo(storageSpecToDeviceInfo(azNode.getStorageSpec()));
                azOverrides.setCgroupSize(azNode.getCgroupSize());
                if (azNode.getMaster() != null) {
                  PerProcessDetails masterOverrides = new PerProcessDetails();
                  masterOverrides.setInstanceType(azNode.getMaster().getInstanceType());
                  masterOverrides.setDeviceInfo(
                      storageSpecToDeviceInfo(azNode.getMaster().getStorageSpec()));
                  azOverrides.getPerProcess().put(ServerType.MASTER, masterOverrides);
                }
                if (azNode.getTserver() != null) {
                  PerProcessDetails tserverOverrides = new PerProcessDetails();
                  tserverOverrides.setInstanceType(azNode.getTserver().getInstanceType());
                  tserverOverrides.setDeviceInfo(
                      storageSpecToDeviceInfo(azNode.getTserver().getStorageSpec()));
                  azOverrides.getPerProcess().put(ServerType.TSERVER, tserverOverrides);
                }
                overrides.getAzOverrides().put(UUID.fromString(azUuid), azOverrides);
              });
    }
    return userIntent;
  }

  DeviceInfo storageSpecToDeviceInfo(ClusterStorageSpec storageSpec);

  K8SNodeResourceSpec toV1K8SNodeResourceSpec(
      api.v2.models.K8SNodeResourceSpec v2K8SNodeResourceSpec);

  ProxyConfig toV1ProxyConfig(NodeProxyConfig nodeProxyConfig);

  ExposingServiceState toV1ExposingServiceState(EnableExposingServiceEnum v2EnableExposingService);

  AuditLogConfig toV1AuditLogConfig(api.v2.models.AuditLogConfig v2AuditLogConfig);

  default UserIntent fillUserIntentFromClusterNetworkingSpec(
      ClusterNetworkingSpec clusterNetworkingSpec, UserIntent userIntent) {
    if (clusterNetworkingSpec == null) {
      return userIntent;
    }
    userIntent.enableLB = clusterNetworkingSpec.getEnableLb();
    userIntent.setProxyConfig(toV1ProxyConfig(clusterNetworkingSpec.getProxyConfig()));
    userIntent.enableExposingService =
        toV1ExposingServiceState(clusterNetworkingSpec.getEnableExposingService());
    return userIntent;
  }

  default UserIntent fillUserIntentFromClusterProviderSpec(
      ClusterProviderSpec clusterProviderSpec, UserIntent userIntent) {
    if (clusterProviderSpec == null) {
      return userIntent;
    }

    userIntent.universeOverrides = clusterProviderSpec.getHelmOverrides();
    Map<String, String> azHelmOverrides = clusterProviderSpec.getAzHelmOverrides();
    if (azHelmOverrides != null) {
      userIntent.azOverrides = new LinkedHashMap<String, String>(azHelmOverrides);
    }
    userIntent.imageBundleUUID = clusterProviderSpec.getImageBundleUuid();
    userIntent.awsArnString = clusterProviderSpec.getAwsInstanceProfile();

    UUID provider = clusterProviderSpec.getProvider();
    if (provider != null) {
      userIntent.provider = provider.toString();
    }
    List<UUID> regionList = clusterProviderSpec.getRegionList();
    if (regionList != null) {
      userIntent.regionList = new ArrayList<UUID>(regionList);
    }
    userIntent.preferredRegion = clusterProviderSpec.getPreferredRegion();
    userIntent.accessKeyCode = clusterProviderSpec.getAccessKeyCode();
    return userIntent;
  }

  default UserIntent fillUserIntentFromClusterProviderEditSpec(
      ClusterProviderEditSpec clusterProviderEditSpec, UserIntent userIntent) {
    if (clusterProviderEditSpec == null) {
      return userIntent;
    }
    List<UUID> regionList = clusterProviderEditSpec.getRegionList();
    if (regionList != null) {
      userIntent.regionList = new ArrayList<UUID>(regionList);
    }
    return userIntent;
  }

  default SpecificGFlags v1SpecificGFlagsFromClusterGFlags(ClusterGFlags v2ClusterGFlags) {
    if (v2ClusterGFlags == null) {
      return new SpecificGFlags();
    }
    SpecificGFlags specificGFlags =
        SpecificGFlags.construct(v2ClusterGFlags.getMaster(), v2ClusterGFlags.getTserver());
    if (v2ClusterGFlags.getAzGflags() != null) {
      Map<UUID, PerProcessFlags> perAz = new HashMap<>();
      for (Entry<String, AvailabilityZoneGFlags> entry : v2ClusterGFlags.getAzGflags().entrySet()) {
        PerProcessFlags perProc = new PerProcessFlags();
        perProc.value.put(ServerType.MASTER, entry.getValue().getMaster());
        perProc.value.put(ServerType.TSERVER, entry.getValue().getTserver());
        perAz.put(UUID.fromString(entry.getKey()), perProc);
      }
      specificGFlags.setPerAZ(perAz);
    }
    return specificGFlags;
  }

  default SpecificGFlags clusterSpecToSpecificGFlags(ClusterSpec clusterSpec) {
    SpecificGFlags specificGFlags = v1SpecificGFlagsFromClusterGFlags(clusterSpec.getGflags());
    if (clusterSpec.getClusterType() != null
        && !clusterSpec.getClusterType().equals(ClusterTypeEnum.PRIMARY)) {
      // do not set inherit from primary as v2 manually defaults to true behaviour
      specificGFlags.setInheritFromPrimary(false);
    }
    return specificGFlags;
  }

  default SpecificGFlags clusterAddSpecToSpecificGFlags(ClusterAddSpec clusterAddSpec) {
    SpecificGFlags specificGFlags = v1SpecificGFlagsFromClusterGFlags(clusterAddSpec.getGflags());
    // do not set inherit from primary as v2 manually defaults to true behaviour
    specificGFlags.setInheritFromPrimary(false);
    return specificGFlags;
  }
}
