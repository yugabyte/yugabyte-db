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
import com.yugabyte.yw.models.Universe;
import io.yugabyte.operator.v1alpha1.YBUniverseSpec;
import io.yugabyte.operator.v1alpha1.ybuniversespec.DeviceInfo;
import io.yugabyte.operator.v1alpha1.ybuniversespec.GFlags;
import io.yugabyte.operator.v1alpha1.ybuniversespec.KubernetesOverrides;
import io.yugabyte.operator.v1alpha1.ybuniversespec.MasterDeviceInfo;
import io.yugabyte.operator.v1alpha1.ybuniversespec.ReadReplica;
import io.yugabyte.operator.v1alpha1.ybuniversespec.YbcThrottleParameters;
import io.yugabyte.operator.v1alpha1.ybuniversespec.gflags.PerAZ;
import java.util.HashMap;
import java.util.Map;
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

  public void setDeviceInfoSpecFromUniverse(YBUniverseSpec spec, Universe universe) {
    com.yugabyte.yw.models.helpers.DeviceInfo clusterDeviceInfo =
        universe.getUniverseDetails().getPrimaryCluster().userIntent.deviceInfo;
    if (clusterDeviceInfo == null) {
      log.debug("No device info found for universe {}", universe.getUniverseUUID());
      return;
    }
    DeviceInfo deviceInfo = new DeviceInfo();
    if (clusterDeviceInfo.volumeSize != null) {
      deviceInfo.setVolumeSize(Long.valueOf(clusterDeviceInfo.volumeSize));
    }
    if (clusterDeviceInfo.numVolumes != null) {
      deviceInfo.setNumVolumes(Long.valueOf(clusterDeviceInfo.numVolumes));
    }
    if (clusterDeviceInfo.storageClass != null) {
      deviceInfo.setStorageClass(clusterDeviceInfo.storageClass);
    }
    spec.setDeviceInfo(deviceInfo);
  }

  public void setReadReplicaDeviceInfo(
      ReadReplica spec, UniverseDefinitionTaskParams.Cluster cluster) {
    com.yugabyte.yw.models.helpers.DeviceInfo clusterDeviceInfo = cluster.userIntent.deviceInfo;
    if (clusterDeviceInfo == null) {
      log.debug("No device info found for read replica cluster {}", cluster.uuid);
      return;
    }
    io.yugabyte.operator.v1alpha1.ybuniversespec.readreplica.DeviceInfo deviceInfo =
        new io.yugabyte.operator.v1alpha1.ybuniversespec.readreplica.DeviceInfo();
    if (clusterDeviceInfo.volumeSize != null) {
      deviceInfo.setVolumeSize(Long.valueOf(clusterDeviceInfo.volumeSize));
    }
    if (clusterDeviceInfo.numVolumes != null) {
      deviceInfo.setNumVolumes(Long.valueOf(clusterDeviceInfo.numVolumes));
    }
    spec.setDeviceInfo(deviceInfo);
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

  public void setMasterDeviceInfoSpecFromUniverse(YBUniverseSpec spec, Universe universe) {
    com.yugabyte.yw.models.helpers.DeviceInfo deviceInfo =
        universe.getUniverseDetails().getPrimaryCluster().userIntent.masterDeviceInfo;
    if (deviceInfo == null) {
      log.debug("No master device info found for universe {}", universe.getUniverseUUID());
      return;
    }
    MasterDeviceInfo masterDeviceInfo = new MasterDeviceInfo();
    if (deviceInfo.volumeSize != null) {
      masterDeviceInfo.setVolumeSize(Long.valueOf(deviceInfo.volumeSize));
    }
    if (deviceInfo.storageClass != null) {
      masterDeviceInfo.setStorageClass(deviceInfo.storageClass);
    }
    if (deviceInfo.numVolumes != null) {
      masterDeviceInfo.setNumVolumes(Long.valueOf(deviceInfo.numVolumes));
    }
    spec.setMasterDeviceInfo(masterDeviceInfo);
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
}
