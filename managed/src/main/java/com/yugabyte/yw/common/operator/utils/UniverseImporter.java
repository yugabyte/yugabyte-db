package com.yugabyte.yw.common.operator.utils;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.module.SimpleModule;
import com.fasterxml.jackson.dataformat.yaml.YAMLFactory;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.backuprestore.ybc.YbcManager;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.common.operator.helpers.KubernetesOverridesDeserializer;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent.K8SNodeResourceSpec;
import com.yugabyte.yw.forms.YbcThrottleParametersResponse.ThrottleParamValue;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Universe;
import io.yugabyte.operator.v1alpha1.YBUniverseSpec;
import io.yugabyte.operator.v1alpha1.ybuniversespec.GFlags;
import io.yugabyte.operator.v1alpha1.ybuniversespec.KubernetesOverrides;
import io.yugabyte.operator.v1alpha1.ybuniversespec.MasterVolume;
import io.yugabyte.operator.v1alpha1.ybuniversespec.ReadReplica;
import io.yugabyte.operator.v1alpha1.ybuniversespec.TserverVolume;
import io.yugabyte.operator.v1alpha1.ybuniversespec.YbcThrottleParameters;
import io.yugabyte.operator.v1alpha1.ybuniversespec.gflags.PerAZ;
import java.util.HashMap;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class UniverseImporter {

  private final YbcManager ybcManager;

  @Inject
  public UniverseImporter(YbcManager ybcManager) {
    this.ybcManager = ybcManager;
  }

  public void setYsqlSpec(YBUniverseSpec spec, boolean enabled, boolean authEnabled) {
    spec.setEnableYSQL(enabled);
    if (authEnabled) {
      spec.setEnableYSQLAuth(true);
    }
  }

  public void setYcqlSpec(YBUniverseSpec spec, boolean enabled, boolean authEnabled) {
    spec.setEnableYCQL(enabled);
    if (authEnabled) {
      spec.setEnableYCQLAuth(true);
    }
  }

  public void setGflagsSpecFromUniverse(YBUniverseSpec spec, Universe universe) {
    SpecificGFlags specificGFlags =
        universe.getUniverseDetails().getPrimaryCluster().userIntent.specificGFlags;
    if (specificGFlags == null) {
      log.debug("No specific gflags found for universe {}", universe.getUniverseUUID());
      return;
    }
    GFlags gflags = new GFlags();
    if (specificGFlags.getPerProcessFlags() != null) {
      gflags.setTserverGFlags(specificGFlags.getPerProcessFlags().value.get(ServerType.TSERVER));
      gflags.setMasterGFlags(specificGFlags.getPerProcessFlags().value.get(ServerType.MASTER));
    }

    if (specificGFlags.getPerAZ() != null) {
      Map<String, PerAZ> allPerAZ = new HashMap<>();
      // We may want to check that the az's actually exist in the provider.
      universe
          .getUniverseDetails()
          .getPrimaryCluster()
          .userIntent
          .specificGFlags
          .getPerAZ()
          .entrySet()
          .stream()
          .filter(e -> e.getValue() != null)
          .forEach(
              e -> {
                PerAZ perAZ = new PerAZ();
                perAZ.setTserverGFlags(e.getValue().value.get(ServerType.TSERVER));
                perAZ.setMasterGFlags(e.getValue().value.get(ServerType.MASTER));
                allPerAZ.put(e.getKey().toString(), perAZ);
              });
      gflags.setPerAZ(allPerAZ);
    }
    spec.setGFlags(gflags);
  }

  public void setTserverVolumeSpecFromUniverse(YBUniverseSpec spec, Universe universe) {
    com.yugabyte.yw.models.helpers.DeviceInfo clusterDeviceInfo =
        universe.getUniverseDetails().getPrimaryCluster().userIntent.deviceInfo;
    if (clusterDeviceInfo == null) {
      log.debug("No device info found for universe {}", universe.getUniverseUUID());
      return;
    }
    TserverVolume tserverVolume = new TserverVolume();
    if (clusterDeviceInfo.volumeSize != null) {
      tserverVolume.setVolumeSize(Long.valueOf(clusterDeviceInfo.volumeSize));
    }
    if (clusterDeviceInfo.numVolumes != null) {
      tserverVolume.setNumVolumes(Long.valueOf(clusterDeviceInfo.numVolumes));
    }
    if (clusterDeviceInfo.storageClass != null) {
      tserverVolume.setStorageClass(clusterDeviceInfo.storageClass);
    }
    spec.setTserverVolume(tserverVolume);
  }

  public void setReadReplicaTserverVolume(
      ReadReplica spec, UniverseDefinitionTaskParams.Cluster cluster) {
    com.yugabyte.yw.models.helpers.DeviceInfo clusterDeviceInfo = cluster.userIntent.deviceInfo;
    if (clusterDeviceInfo == null) {
      log.debug("No device info found for read replica cluster {}", cluster.uuid);
      return;
    }
    io.yugabyte.operator.v1alpha1.ybuniversespec.readreplica.TserverVolume tserverVolume =
        new io.yugabyte.operator.v1alpha1.ybuniversespec.readreplica.TserverVolume();
    if (clusterDeviceInfo.volumeSize != null) {
      tserverVolume.setVolumeSize(Long.valueOf(clusterDeviceInfo.volumeSize));
    }
    if (clusterDeviceInfo.numVolumes != null) {
      tserverVolume.setNumVolumes(Long.valueOf(clusterDeviceInfo.numVolumes));
    }
    if (clusterDeviceInfo.storageClass != null) {
      tserverVolume.setStorageClass(clusterDeviceInfo.storageClass);
    }
    spec.setTserverVolume(tserverVolume);
  }

  public void setTserverResourceSpecFromUniverse(YBUniverseSpec spec, Universe universe) {
    io.yugabyte.operator.v1alpha1.ybuniversespec.TserverResourceSpec resourceSpec =
        new io.yugabyte.operator.v1alpha1.ybuniversespec.TserverResourceSpec();
    K8SNodeResourceSpec universeSpec =
        universe.getUniverseDetails().getPrimaryCluster().userIntent.tserverK8SNodeResourceSpec;
    if (universeSpec == null) {
      log.debug("No resource spec found for tserver cluster {}", universe.getUniverseUUID());
      return;
    }
    if (universeSpec.cpuCoreCount != null) {
      resourceSpec.setCpu(universeSpec.cpuCoreCount);
    }
    if (universeSpec.memoryGib != null) {
      resourceSpec.setMemory(universeSpec.memoryGib);
    }
    spec.setTserverResourceSpec(resourceSpec);
  }

  public void setMasterResourceSpecFromUniverse(YBUniverseSpec spec, Universe universe) {
    K8SNodeResourceSpec universeSpec =
        universe.getUniverseDetails().getPrimaryCluster().userIntent.masterK8SNodeResourceSpec;
    if (universeSpec == null) {
      log.debug("No resource spec found for master cluster {}", universe.getUniverseUUID());
      return;
    }
    io.yugabyte.operator.v1alpha1.ybuniversespec.MasterResourceSpec resourceSpec =
        new io.yugabyte.operator.v1alpha1.ybuniversespec.MasterResourceSpec();
    if (universeSpec.cpuCoreCount != null) {
      resourceSpec.setCpu(universeSpec.cpuCoreCount);
    }
    if (universeSpec.memoryGib != null) {
      resourceSpec.setMemory(universeSpec.memoryGib);
    }
    spec.setMasterResourceSpec(resourceSpec);
  }

  public void setReadReplicaResourceSpecFromUniverse(
      ReadReplica spec, UniverseDefinitionTaskParams.Cluster cluster) {
    io.yugabyte.operator.v1alpha1.ybuniversespec.readreplica.TserverResourceSpec resourceSpec =
        new io.yugabyte.operator.v1alpha1.ybuniversespec.readreplica.TserverResourceSpec();
    if (cluster.userIntent.tserverK8SNodeResourceSpec == null) {
      log.debug("No resource spec found for read replica cluster {}", cluster.uuid);
      return;
    }
    if (cluster.userIntent.tserverK8SNodeResourceSpec.cpuCoreCount != null) {
      resourceSpec.setCpu(cluster.userIntent.tserverK8SNodeResourceSpec.cpuCoreCount);
    }
    if (cluster.userIntent.tserverK8SNodeResourceSpec.memoryGib != null) {
      resourceSpec.setMemory(cluster.userIntent.tserverK8SNodeResourceSpec.memoryGib);
    }
    spec.setTserverResourceSpec(resourceSpec);
  }

  public void setMasterVolumeSpecFromUniverse(YBUniverseSpec spec, Universe universe) {
    com.yugabyte.yw.models.helpers.DeviceInfo deviceInfo =
        universe.getUniverseDetails().getPrimaryCluster().userIntent.masterDeviceInfo;
    if (deviceInfo == null) {
      log.debug("No master device info found for universe {}", universe.getUniverseUUID());
      return;
    }
    MasterVolume masterVolume = new MasterVolume();
    if (deviceInfo.volumeSize != null) {
      masterVolume.setVolumeSize(Long.valueOf(deviceInfo.volumeSize));
    }
    if (deviceInfo.storageClass != null) {
      masterVolume.setStorageClass(deviceInfo.storageClass);
    }
    if (deviceInfo.numVolumes != null) {
      masterVolume.setNumVolumes(Long.valueOf(deviceInfo.numVolumes));
    }
    spec.setMasterVolume(masterVolume);
  }

  public void setYbcThrottleParametersSpecFromUniverse(YBUniverseSpec spec, Universe universe) {
    YbcThrottleParameters throttleParameters = new YbcThrottleParameters();
    Map<String, ThrottleParamValue> throttleParams =
        ybcManager.getThrottleParams(universe.getUniverseUUID()).getThrottleParamsMap();
    if (throttleParams == null) {
      log.debug("No throttle params found for universe {}", universe.getUniverseUUID());
      return;
    }
    if (throttleParams.get(GFlagsUtil.YBC_MAX_CONCURRENT_UPLOADS) != null) {
      throttleParameters.setMaxConcurrentUploads(
          throttleParams.get(GFlagsUtil.YBC_MAX_CONCURRENT_UPLOADS).getCurrentValue());
    }
    if (throttleParams.get(GFlagsUtil.YBC_PER_UPLOAD_OBJECTS) != null) {
      throttleParameters.setPerUploadNumObjects(
          throttleParams.get(GFlagsUtil.YBC_PER_UPLOAD_OBJECTS).getCurrentValue());
    }
    if (throttleParams.get(GFlagsUtil.YBC_MAX_CONCURRENT_DOWNLOADS) != null) {
      throttleParameters.setMaxConcurrentDownloads(
          throttleParams.get(GFlagsUtil.YBC_MAX_CONCURRENT_DOWNLOADS).getCurrentValue());
    }
    if (throttleParams.get(GFlagsUtil.YBC_PER_DOWNLOAD_OBJECTS) != null) {
      throttleParameters.setPerDownloadNumObjects(
          throttleParams.get(GFlagsUtil.YBC_PER_DOWNLOAD_OBJECTS).getCurrentValue());
    }
    if (throttleParams.get(GFlagsUtil.YBC_DISK_READ_BYTES_PER_SECOND) != null) {
      throttleParameters.setDiskReadBytesPerSec(
          throttleParams.get(GFlagsUtil.YBC_DISK_READ_BYTES_PER_SECOND).getCurrentValue());
    }
    if (throttleParams.get(GFlagsUtil.YBC_DISK_WRITE_BYTES_PER_SECOND) != null) {
      throttleParameters.setDiskWriteBytesPerSec(
          throttleParams.get(GFlagsUtil.YBC_DISK_WRITE_BYTES_PER_SECOND).getCurrentValue());
    }
    spec.setYbcThrottleParameters(throttleParameters);
  }

  public void setKubernetesOverridesSpecFromUniverse(YBUniverseSpec spec, Universe universe) {
    if (universe.getUniverseDetails().getPrimaryCluster().userIntent.universeOverrides == null
        || universe
            .getUniverseDetails()
            .getPrimaryCluster()
            .userIntent
            .universeOverrides
            .isEmpty()) {
      log.trace("No KubernetesOverrides found for universe {}", universe.getUniverseUUID());
      return;
    }
    SimpleModule module = new SimpleModule();
    module.addDeserializer(KubernetesOverrides.class, new KubernetesOverridesDeserializer());
    ObjectMapper mapper = new ObjectMapper(new YAMLFactory());
    mapper.registerModule(module);
    try {
      spec.setKubernetesOverrides(
          mapper.readValue(
              universe.getUniverseDetails().getPrimaryCluster().userIntent.universeOverrides,
              KubernetesOverrides.class));
    } catch (JsonProcessingException e) {
      throw new RuntimeException("Error in setting KubernetesOverridesSpecFromUniverse", e);
    }
  }

  public void setReadReplicaPlacementInfo(
      ReadReplica spec, UniverseDefinitionTaskParams.Cluster cluster) {
    io.yugabyte.operator.v1alpha1.ybuniversespec.readreplica.PlacementInfo placementInfo =
        new io.yugabyte.operator.v1alpha1.ybuniversespec.readreplica.PlacementInfo();
    placementInfo.setRegions(
        cluster.placementInfo.cloudList.get(0).regionList.stream()
            .map(
                region -> {
                  io.yugabyte.operator.v1alpha1.ybuniversespec.readreplica.placementinfo.Regions
                      regionInfo =
                          new io.yugabyte.operator.v1alpha1.ybuniversespec.readreplica.placementinfo
                              .Regions();
                  regionInfo.setCode(region.code);
                  regionInfo.setZones(
                      region.azList.stream()
                          .map(
                              zone -> {
                                io.yugabyte.operator.v1alpha1.ybuniversespec.readreplica
                                        .placementinfo.regions.Zones
                                    zoneInfo =
                                        new io.yugabyte.operator.v1alpha1.ybuniversespec.readreplica
                                            .placementinfo.regions.Zones();
                                zoneInfo.setCode(zone.name);
                                zoneInfo.setNumNodes(Long.valueOf(zone.numNodesInAZ));
                                return zoneInfo;
                              })
                          .collect(Collectors.toList()));
                  return regionInfo;
                })
            .collect(Collectors.toList()));
    spec.setPlacementInfo(placementInfo);
  }

  public void setAzDeviceInfoOverridesSpecFromUniverse(YBUniverseSpec spec, Universe universe) {
    UniverseDefinitionTaskParams.UserIntent userIntent =
        universe.getUniverseDetails().getPrimaryCluster().userIntent;
    if (userIntent.getUserIntentOverrides() == null
        || userIntent.getUserIntentOverrides().getAzOverrides() == null
        || userIntent.getUserIntentOverrides().getAzOverrides().isEmpty()) {
      log.debug("No az device info overrides found for universe {}", universe.getUniverseUUID());
      return;
    }

    // Get or create tserverVolume and masterVolume (they should already exist from previous calls)
    TserverVolume tserverVolume = spec.getTserverVolume();
    MasterVolume masterVolume = spec.getMasterVolume();

    Map<String, io.yugabyte.operator.v1alpha1.ybuniversespec.tservervolume.PerAZ> tserverPerAZMap =
        null;
    Map<String, io.yugabyte.operator.v1alpha1.ybuniversespec.mastervolume.PerAZ> masterPerAZMap =
        null;

    // First pass: collect all overrides to determine if we need to create volume objects
    boolean hasTserverOverrides = false;
    boolean hasMasterOverrides = false;
    for (Map.Entry<UUID, UniverseDefinitionTaskParams.AZOverrides> entry :
        userIntent.getUserIntentOverrides().getAzOverrides().entrySet()) {
      UniverseDefinitionTaskParams.AZOverrides azOverrides = entry.getValue();
      if (azOverrides.getPerProcess() != null
          && azOverrides.getPerProcess().containsKey(ServerType.TSERVER)
          && azOverrides.getPerProcess().get(ServerType.TSERVER).getDeviceInfo() != null) {
        com.yugabyte.yw.models.helpers.DeviceInfo tserverDeviceInfo =
            azOverrides.getPerProcess().get(ServerType.TSERVER).getDeviceInfo();
        if (tserverDeviceInfo != null && !tserverDeviceInfo.allNull()) {
          hasTserverOverrides = true;
        }
      }
      if (azOverrides.getPerProcess() != null
          && azOverrides.getPerProcess().containsKey(ServerType.MASTER)
          && azOverrides.getPerProcess().get(ServerType.MASTER).getDeviceInfo() != null) {
        com.yugabyte.yw.models.helpers.DeviceInfo masterDeviceInfo =
            azOverrides.getPerProcess().get(ServerType.MASTER).getDeviceInfo();
        if (masterDeviceInfo != null && !masterDeviceInfo.allNull()) {
          hasMasterOverrides = true;
        }
      }
    }

    // Only create volume objects if we have overrides and they don't already exist
    if (hasTserverOverrides) {
      if (tserverVolume == null) {
        tserverVolume = new TserverVolume();
        spec.setTserverVolume(tserverVolume);
      }
      tserverPerAZMap = tserverVolume.getPerAZ();
      if (tserverPerAZMap == null) {
        tserverPerAZMap = new HashMap<>();
        tserverVolume.setPerAZ(tserverPerAZMap);
      }
    }

    if (hasMasterOverrides) {
      if (masterVolume == null) {
        masterVolume = new MasterVolume();
        spec.setMasterVolume(masterVolume);
      }
      masterPerAZMap = masterVolume.getPerAZ();
      if (masterPerAZMap == null) {
        masterPerAZMap = new HashMap<>();
        masterVolume.setPerAZ(masterPerAZMap);
      }
    }

    // Second pass: populate the perAZ maps
    for (Map.Entry<UUID, UniverseDefinitionTaskParams.AZOverrides> entry :
        userIntent.getUserIntentOverrides().getAzOverrides().entrySet()) {
      UUID azUUID = entry.getKey();
      UniverseDefinitionTaskParams.AZOverrides azOverrides = entry.getValue();

      try {
        AvailabilityZone az = AvailabilityZone.getOrBadRequest(azUUID);
        String azCode = az.getCode();

        // Handle tserver deviceInfo
        if (tserverPerAZMap != null
            && azOverrides.getPerProcess() != null
            && azOverrides.getPerProcess().containsKey(ServerType.TSERVER)
            && azOverrides.getPerProcess().get(ServerType.TSERVER).getDeviceInfo() != null) {
          com.yugabyte.yw.models.helpers.DeviceInfo tserverDeviceInfo =
              azOverrides.getPerProcess().get(ServerType.TSERVER).getDeviceInfo();
          if (tserverDeviceInfo != null && !tserverDeviceInfo.allNull()) {
            io.yugabyte.operator.v1alpha1.ybuniversespec.tservervolume.PerAZ tserverPerAZ =
                new io.yugabyte.operator.v1alpha1.ybuniversespec.tservervolume.PerAZ();
            if (tserverDeviceInfo.volumeSize != null) {
              tserverPerAZ.setVolumeSize(Long.valueOf(tserverDeviceInfo.volumeSize));
            }
            if (tserverDeviceInfo.numVolumes != null) {
              tserverPerAZ.setNumVolumes(Long.valueOf(tserverDeviceInfo.numVolumes));
            }
            if (tserverDeviceInfo.storageClass != null) {
              tserverPerAZ.setStorageClass(tserverDeviceInfo.storageClass);
            }
            tserverPerAZMap.put(azCode, tserverPerAZ);
          }
        }

        // Handle master deviceInfo
        if (masterPerAZMap != null
            && azOverrides.getPerProcess() != null
            && azOverrides.getPerProcess().containsKey(ServerType.MASTER)
            && azOverrides.getPerProcess().get(ServerType.MASTER).getDeviceInfo() != null) {
          com.yugabyte.yw.models.helpers.DeviceInfo masterDeviceInfo =
              azOverrides.getPerProcess().get(ServerType.MASTER).getDeviceInfo();
          if (masterDeviceInfo != null && !masterDeviceInfo.allNull()) {
            io.yugabyte.operator.v1alpha1.ybuniversespec.mastervolume.PerAZ masterPerAZ =
                new io.yugabyte.operator.v1alpha1.ybuniversespec.mastervolume.PerAZ();
            if (masterDeviceInfo.volumeSize != null) {
              masterPerAZ.setVolumeSize(Long.valueOf(masterDeviceInfo.volumeSize));
            }
            if (masterDeviceInfo.numVolumes != null) {
              masterPerAZ.setNumVolumes(Long.valueOf(masterDeviceInfo.numVolumes));
            }
            if (masterDeviceInfo.storageClass != null) {
              masterPerAZ.setStorageClass(masterDeviceInfo.storageClass);
            }
            masterPerAZMap.put(azCode, masterPerAZ);
          }
        }
      } catch (Exception e) {
        log.warn(
            "Failed to process az device info override for AZ UUID {}: {}", azUUID, e.getMessage());
      }
    }
  }

  public void setReadReplicaAzDeviceInfoOverrides(
      ReadReplica spec, UniverseDefinitionTaskParams.Cluster cluster) {
    UniverseDefinitionTaskParams.UserIntent userIntent = cluster.userIntent;
    if (userIntent.getUserIntentOverrides() == null
        || userIntent.getUserIntentOverrides().getAzOverrides() == null
        || userIntent.getUserIntentOverrides().getAzOverrides().isEmpty()) {
      log.debug("No az device info overrides found for read replica cluster {}", cluster.uuid);
      return;
    }

    // Get or create tserverVolume
    io.yugabyte.operator.v1alpha1.ybuniversespec.readreplica.TserverVolume tserverVolume =
        spec.getTserverVolume();
    if (tserverVolume == null) {
      tserverVolume = new io.yugabyte.operator.v1alpha1.ybuniversespec.readreplica.TserverVolume();
      spec.setTserverVolume(tserverVolume);
    }

    Map<String, io.yugabyte.operator.v1alpha1.ybuniversespec.readreplica.tservervolume.PerAZ>
        tserverPerAZMap = tserverVolume.getPerAZ();
    if (tserverPerAZMap == null) {
      tserverPerAZMap = new HashMap<>();
      tserverVolume.setPerAZ(tserverPerAZMap);
    }

    for (Map.Entry<UUID, UniverseDefinitionTaskParams.AZOverrides> entry :
        userIntent.getUserIntentOverrides().getAzOverrides().entrySet()) {
      UUID azUUID = entry.getKey();
      UniverseDefinitionTaskParams.AZOverrides azOverrides = entry.getValue();

      try {
        AvailabilityZone az = AvailabilityZone.getOrBadRequest(azUUID);
        String azCode = az.getCode();

        // Handle tserver deviceInfo (read replica only has tserver)
        if (azOverrides.getPerProcess() != null
            && azOverrides.getPerProcess().containsKey(ServerType.TSERVER)
            && azOverrides.getPerProcess().get(ServerType.TSERVER).getDeviceInfo() != null) {
          com.yugabyte.yw.models.helpers.DeviceInfo tserverDeviceInfo =
              azOverrides.getPerProcess().get(ServerType.TSERVER).getDeviceInfo();
          if (tserverDeviceInfo != null && !tserverDeviceInfo.allNull()) {
            io.yugabyte.operator.v1alpha1.ybuniversespec.readreplica.tservervolume.PerAZ
                tserverPerAZ =
                    new io.yugabyte.operator.v1alpha1.ybuniversespec.readreplica.tservervolume
                        .PerAZ();
            if (tserverDeviceInfo.volumeSize != null) {
              tserverPerAZ.setVolumeSize(Long.valueOf(tserverDeviceInfo.volumeSize));
            }
            if (tserverDeviceInfo.numVolumes != null) {
              tserverPerAZ.setNumVolumes(Long.valueOf(tserverDeviceInfo.numVolumes));
            }
            if (tserverDeviceInfo.storageClass != null) {
              tserverPerAZ.setStorageClass(tserverDeviceInfo.storageClass);
            }
            tserverPerAZMap.put(azCode, tserverPerAZ);
          }
        }
      } catch (Exception e) {
        log.warn(
            "Failed to process az device info override for read replica AZ UUID {}: {}",
            azUUID,
            e.getMessage());
      }
    }
  }
}
