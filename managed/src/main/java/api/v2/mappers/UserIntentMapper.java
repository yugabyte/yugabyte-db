// Copyright (c) YugaByte, Inc.
package api.v2.mappers;

import api.v2.models.AvailabilityZoneGFlags;
import api.v2.models.ClusterAddSpec;
import api.v2.models.ClusterCustomInstanceSpec;
import api.v2.models.ClusterEditSpec;
import api.v2.models.ClusterGFlags;
import api.v2.models.ClusterNetworkingSpec;
import api.v2.models.ClusterSpec;
import api.v2.models.ClusterSpec.ClusterTypeEnum;
import api.v2.models.ClusterStorageSpec;
import api.v2.models.ClusterStorageSpec.StorageTypeEnum;
import com.yugabyte.yw.cloud.PublicCloudConstants.StorageType;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.common.gflags.SpecificGFlags.PerProcessFlags;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import java.util.HashMap;
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

  @Mapping(target = ".", source = "userIntent.deviceInfo")
  ClusterStorageSpec userIntentToClusterStorageSpec(
      UniverseDefinitionTaskParams.UserIntent userIntent);

  @ValueMappings({
    @ValueMapping(target = "SCRATCH", source = "Scratch"),
    @ValueMapping(target = "PERSISTENT", source = "Persistent"),
    @ValueMapping(target = "STANDARDSSD_LRS", source = "StandardSSD_LRS"),
    @ValueMapping(target = "PREMIUM_LRS", source = "Premium_LRS"),
    @ValueMapping(target = "ULTRASSD_LRS", source = "UltraSSD_LRS"),
    @ValueMapping(target = "LOCAL", source = "Local"),
  })
  StorageTypeEnum mapStorageType(StorageType storageType);

  @Mapping(target = "enableLb", source = "enableLB")
  ClusterNetworkingSpec userIntentToClusterNetworkingSpec(
      UniverseDefinitionTaskParams.UserIntent userIntent);

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

  @Mapping(target = "master", source = "masterK8SNodeResourceSpec")
  @Mapping(target = "tserver", source = "tserverK8SNodeResourceSpec")
  ClusterCustomInstanceSpec userIntentToClusterCustomInstanceSpec(UserIntent userIntent);

  // inverse mapping
  @Mapping(target = "deviceInfo", source = "storageSpec")
  @Mapping(target = ".", source = "networkingSpec")
  @Mapping(target = "enableLB", source = "networkingSpec.enableLb")
  @Mapping(target = ".", source = "providerSpec")
  @Mapping(target = "universeOverrides", source = "providerSpec.helmOverrides")
  @Mapping(target = "azOverrides", source = "providerSpec.azHelmOverrides")
  @Mapping(target = "imageBundleUUID", source = "providerSpec.imageBundleUuid")
  @Mapping(target = "specificGFlags", source = "clusterSpec")
  @Mapping(target = "masterK8SNodeResourceSpec", source = "customInstanceSpec.master")
  @Mapping(target = "tserverK8SNodeResourceSpec", source = "customInstanceSpec.tserver")
  UserIntent toV1UserIntent(ClusterSpec clusterSpec);

  @Mapping(target = "deviceInfo", source = "storageSpec")
  @Mapping(target = ".", source = "providerSpec")
  @Mapping(target = "masterK8SNodeResourceSpec", source = "customInstanceSpec.master")
  @Mapping(target = "tserverK8SNodeResourceSpec", source = "customInstanceSpec.tserver")
  UserIntent toV1UserIntentFromClusterEditSpec(
      ClusterEditSpec clusterEditSpec, @MappingTarget UserIntent userIntent);

  @Mapping(target = "deviceInfo", source = "storageSpec")
  @Mapping(target = ".", source = "providerSpec")
  @Mapping(target = "specificGFlags", source = "clusterAddSpec")
  @Mapping(target = "masterK8SNodeResourceSpec", source = "customInstanceSpec.master")
  @Mapping(target = "tserverK8SNodeResourceSpec", source = "customInstanceSpec.tserver")
  UserIntent overwriteUserIntentFromClusterAddSpec(
      ClusterAddSpec clusterAddSpec, @MappingTarget UserIntent userIntent);

  @InheritInverseConfiguration
  StorageType mapStorageTypeEnum(StorageTypeEnum storageType);

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
