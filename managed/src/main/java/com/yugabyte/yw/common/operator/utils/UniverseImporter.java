package com.yugabyte.yw.common.operator.utils;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.module.SimpleModule;
import com.fasterxml.jackson.dataformat.yaml.YAMLFactory;
import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.backuprestore.ybc.YbcManager;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.common.operator.helpers.KubernetesOverridesDeserializer;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
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
    GFlags gflags = new GFlags();
    gflags.setTserverGFlags(
        universe
            .getUniverseDetails()
            .getPrimaryCluster()
            .userIntent
            .specificGFlags
            .getPerProcessFlags()
            .value
            .get(ServerType.TSERVER));
    gflags.setMasterGFlags(
        universe
            .getUniverseDetails()
            .getPrimaryCluster()
            .userIntent
            .specificGFlags
            .getPerProcessFlags()
            .value
            .get(ServerType.MASTER));

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
        .forEach(
            e -> {
              PerAZ perAZ = new PerAZ();
              perAZ.setTserverGFlags(e.getValue().value.get(ServerType.TSERVER));
              perAZ.setMasterGFlags(e.getValue().value.get(ServerType.MASTER));
              allPerAZ.put(e.getKey().toString(), perAZ);
            });
    gflags.setPerAZ(allPerAZ);
    spec.setGFlags(gflags);
  }

  public void setDeviceInfoSpecFromUniverse(YBUniverseSpec spec, Universe universe) {
    DeviceInfo deviceInfo = new DeviceInfo();
    deviceInfo.setVolumeSize(
        Long.valueOf(
            universe.getUniverseDetails().getPrimaryCluster().userIntent.deviceInfo.volumeSize));
    deviceInfo.setNumVolumes(
        Long.valueOf(
            universe.getUniverseDetails().getPrimaryCluster().userIntent.deviceInfo.numVolumes));
    deviceInfo.setStorageClass(
        universe.getUniverseDetails().getPrimaryCluster().userIntent.deviceInfo.storageClass);
    spec.setDeviceInfo(deviceInfo);
  }

  public void setReadReplicaDeviceInfo(
      ReadReplica spec, UniverseDefinitionTaskParams.Cluster cluster) {
    io.yugabyte.operator.v1alpha1.ybuniversespec.readreplica.DeviceInfo deviceInfo =
        new io.yugabyte.operator.v1alpha1.ybuniversespec.readreplica.DeviceInfo();
    deviceInfo.setVolumeSize(Long.valueOf(cluster.userIntent.deviceInfo.volumeSize));
    deviceInfo.setNumVolumes(Long.valueOf(cluster.userIntent.deviceInfo.numVolumes));
    spec.setDeviceInfo(deviceInfo);
  }

  public void setTserverResourceSpecFromUniverse(YBUniverseSpec spec, Universe universe) {
    io.yugabyte.operator.v1alpha1.ybuniversespec.TserverResourceSpec resourceSpec =
        new io.yugabyte.operator.v1alpha1.ybuniversespec.TserverResourceSpec();
    resourceSpec.setCpu(
        universe
            .getUniverseDetails()
            .getPrimaryCluster()
            .userIntent
            .tserverK8SNodeResourceSpec
            .cpuCoreCount);
    resourceSpec.setMemory(
        universe
            .getUniverseDetails()
            .getPrimaryCluster()
            .userIntent
            .tserverK8SNodeResourceSpec
            .memoryGib);
    spec.setTserverResourceSpec(resourceSpec);
  }

  public void setMasterResourceSpecFromUniverse(YBUniverseSpec spec, Universe universe) {
    io.yugabyte.operator.v1alpha1.ybuniversespec.MasterResourceSpec resourceSpec =
        new io.yugabyte.operator.v1alpha1.ybuniversespec.MasterResourceSpec();
    resourceSpec.setCpu(
        universe
            .getUniverseDetails()
            .getPrimaryCluster()
            .userIntent
            .masterK8SNodeResourceSpec
            .cpuCoreCount);
    resourceSpec.setMemory(
        universe
            .getUniverseDetails()
            .getPrimaryCluster()
            .userIntent
            .masterK8SNodeResourceSpec
            .memoryGib);
    spec.setMasterResourceSpec(resourceSpec);
  }

  public void setReadReplicaResourceSpecFromUniverse(
      ReadReplica spec, UniverseDefinitionTaskParams.Cluster cluster) {
    io.yugabyte.operator.v1alpha1.ybuniversespec.readreplica.TserverResourceSpec resourceSpec =
        new io.yugabyte.operator.v1alpha1.ybuniversespec.readreplica.TserverResourceSpec();
    resourceSpec.setCpu(cluster.userIntent.tserverK8SNodeResourceSpec.cpuCoreCount);
    resourceSpec.setMemory(cluster.userIntent.tserverK8SNodeResourceSpec.memoryGib);
    spec.setTserverResourceSpec(resourceSpec);
  }

  public void setMasterDeviceInfoSpecFromUniverse(YBUniverseSpec spec, Universe universe) {
    MasterDeviceInfo masterDeviceInfo = new MasterDeviceInfo();
    masterDeviceInfo.setVolumeSize(
        Long.valueOf(
            universe
                .getUniverseDetails()
                .getPrimaryCluster()
                .userIntent
                .masterDeviceInfo
                .volumeSize));
    masterDeviceInfo.setNumVolumes(
        Long.valueOf(
            universe
                .getUniverseDetails()
                .getPrimaryCluster()
                .userIntent
                .masterDeviceInfo
                .numVolumes));
    masterDeviceInfo.setStorageClass(
        universe.getUniverseDetails().getPrimaryCluster().userIntent.masterDeviceInfo.storageClass);
    spec.setMasterDeviceInfo(masterDeviceInfo);
  }

  public void setYbcThrottleParametersSpecFromUniverse(YBUniverseSpec spec, Universe universe) {
    YbcThrottleParameters throttleParameters = new YbcThrottleParameters();
    Map<String, ThrottleParamValue> throttleParams =
        ybcManager.getThrottleParams(universe.getUniverseUUID()).getThrottleParamsMap();
    throttleParameters.setMaxConcurrentUploads(
        throttleParams.get(GFlagsUtil.YBC_MAX_CONCURRENT_UPLOADS).getCurrentValue());
    throttleParameters.setPerUploadNumObjects(
        throttleParams.get(GFlagsUtil.YBC_PER_UPLOAD_OBJECTS).getCurrentValue());
    throttleParameters.setMaxConcurrentDownloads(
        throttleParams.get(GFlagsUtil.YBC_MAX_CONCURRENT_DOWNLOADS).getCurrentValue());
    throttleParameters.setPerDownloadNumObjects(
        throttleParams.get(GFlagsUtil.YBC_PER_DOWNLOAD_OBJECTS).getCurrentValue());
    throttleParameters.setDiskReadBytesPerSec(
        throttleParams.get(GFlagsUtil.YBC_DISK_READ_BYTES_PER_SECOND).getCurrentValue());
    throttleParameters.setDiskWriteBytesPerSec(
        throttleParams.get(GFlagsUtil.YBC_DISK_WRITE_BYTES_PER_SECOND).getCurrentValue());
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
