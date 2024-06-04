// Copyright (c) YugaByte, Inc.
package api.v2.mappers;

import api.v2.models.AvailabilityZoneGFlags;
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

  // inverse mapping
  @Mapping(target = "deviceInfo", source = "storageSpec")
  @Mapping(target = ".", source = "networkingSpec")
  @Mapping(target = "enableLB", source = "networkingSpec.enableLb")
  @Mapping(target = ".", source = "providerSpec")
  @Mapping(target = "imageBundleUUID", source = "providerSpec.imageBundleUuid")
  @Mapping(target = "specificGFlags", source = "clusterSpec")
  UserIntent toV1UserIntent(ClusterSpec clusterSpec);

  @InheritInverseConfiguration
  StorageType mapStorageTypeEnum(StorageTypeEnum storageType);

  default SpecificGFlags clusterGFlagsToSpecificGFlags(ClusterSpec clusterSpec) {
    ClusterGFlags clusterGFlags = clusterSpec.getGflags();
    if (clusterGFlags == null) {
      return null;
    }
    SpecificGFlags specificGFlags =
        SpecificGFlags.construct(clusterGFlags.getMaster(), clusterGFlags.getTserver());
    if (clusterGFlags.getAzGflags() != null) {
      Map<UUID, PerProcessFlags> perAz = new HashMap<>();
      for (Entry<String, AvailabilityZoneGFlags> entry : clusterGFlags.getAzGflags().entrySet()) {
        PerProcessFlags perProc = new PerProcessFlags();
        perProc.value.put(ServerType.MASTER, entry.getValue().getMaster());
        perProc.value.put(ServerType.TSERVER, entry.getValue().getTserver());
        perAz.put(UUID.fromString(entry.getKey()), perProc);
      }
      specificGFlags.setPerAZ(perAz);
    }
    if (clusterSpec.getClusterType() != null
        && !clusterSpec.getClusterType().equals(ClusterTypeEnum.PRIMARY)) {
      specificGFlags.setInheritFromPrimary(true);
    }
    return specificGFlags;
  }
}
