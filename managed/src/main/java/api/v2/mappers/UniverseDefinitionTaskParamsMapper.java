// Copyright (c) YugaByte, Inc.
package api.v2.mappers;

import api.v2.models.CloudSpecificInfo;
import api.v2.models.EncryptionAtRestInfo;
import api.v2.models.EncryptionAtRestSpec;
import api.v2.models.EncryptionInTransitSpec;
import api.v2.models.NodeDetails;
import api.v2.models.NodeDetails.MasterStateEnum;
import api.v2.models.UniverseCreateSpec;
import api.v2.models.UniverseEditSpec;
import api.v2.models.UniverseInfo;
import api.v2.models.UniverseSpec;
import api.v2.models.YCQLSpec;
import api.v2.models.YSQLSpec;
import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.forms.CertsRotateParams;
import com.yugabyte.yw.forms.EncryptionAtRestConfig;
import com.yugabyte.yw.forms.FinalizeUpgradeParams;
import com.yugabyte.yw.forms.GFlagsUpgradeParams;
import com.yugabyte.yw.forms.KubernetesGFlagsUpgradeParams;
import com.yugabyte.yw.forms.RestartTaskParams;
import com.yugabyte.yw.forms.RollbackUpgradeParams;
import com.yugabyte.yw.forms.SoftwareUpgradeParams;
import com.yugabyte.yw.forms.SystemdUpgradeParams;
import com.yugabyte.yw.forms.ThirdpartySoftwareUpgradeParams;
import com.yugabyte.yw.forms.TlsToggleParams;
import com.yugabyte.yw.forms.UniverseConfigureTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.models.helpers.NodeDetails.MasterState;
import com.yugabyte.yw.models.helpers.TaskType;
import java.time.OffsetDateTime;
import java.time.ZoneOffset;
import java.util.Date;
import org.mapstruct.BeanMapping;
import org.mapstruct.DecoratedWith;
import org.mapstruct.InheritInverseConfiguration;
import org.mapstruct.Mapper;
import org.mapstruct.Mapping;
import org.mapstruct.MappingTarget;
import org.mapstruct.NullValueMappingStrategy;
import org.mapstruct.ValueMapping;
import org.mapstruct.ValueMappings;
import org.mapstruct.control.DeepClone;
import org.mapstruct.factory.Mappers;

@DecoratedWith(UniverseDefinitionTaskParamsDecorator.class)
@Mapper(
    config = CentralConfig.class,
    mappingControl = DeepClone.class,
    uses = {ClusterMapper.class})
public interface UniverseDefinitionTaskParamsMapper {
  public static UniverseDefinitionTaskParamsMapper INSTANCE =
      Mappers.getMapper(UniverseDefinitionTaskParamsMapper.class);

  @Mapping(target = "existingLBs", ignore = true)
  @Mapping(target = "primaryCluster", ignore = true)
  @Mapping(target = "TServers", ignore = true)
  @Mapping(target = "readOnlyClusters", ignore = true)
  @Mapping(target = "addOnClusters", ignore = true)
  @Mapping(target = "nonPrimaryClusters", ignore = true)
  public UniverseConfigureTaskParams toUniverseConfigureTaskParams(
      UniverseDefinitionTaskParams source);

  @Mapping(target = "existingLBs", ignore = true)
  @Mapping(target = "primaryCluster", ignore = true)
  @Mapping(target = "TServers", ignore = true)
  @Mapping(target = "readOnlyClusters", ignore = true)
  @Mapping(target = "addOnClusters", ignore = true)
  @Mapping(target = "nonPrimaryClusters", ignore = true)
  public GFlagsUpgradeParams toGFlagsUpgradeParams(UniverseDefinitionTaskParams source);

  @Mapping(target = "existingLBs", ignore = true)
  @Mapping(target = "primaryCluster", ignore = true)
  @Mapping(target = "TServers", ignore = true)
  @Mapping(target = "readOnlyClusters", ignore = true)
  @Mapping(target = "addOnClusters", ignore = true)
  @Mapping(target = "nonPrimaryClusters", ignore = true)
  public KubernetesGFlagsUpgradeParams toKubernetesGFlagsUpgradeParams(
      UniverseDefinitionTaskParams source);

  @Mapping(target = "existingLBs", ignore = true)
  @Mapping(target = "primaryCluster", ignore = true)
  @Mapping(target = "TServers", ignore = true)
  @Mapping(target = "readOnlyClusters", ignore = true)
  @Mapping(target = "addOnClusters", ignore = true)
  @Mapping(target = "nonPrimaryClusters", ignore = true)
  public SoftwareUpgradeParams toSoftwareUpgradeParams(UniverseDefinitionTaskParams source);

  @Mapping(target = "existingLBs", ignore = true)
  @Mapping(target = "primaryCluster", ignore = true)
  @Mapping(target = "TServers", ignore = true)
  @Mapping(target = "readOnlyClusters", ignore = true)
  @Mapping(target = "addOnClusters", ignore = true)
  @Mapping(target = "nonPrimaryClusters", ignore = true)
  public RollbackUpgradeParams toRollbackUpgradeParams(UniverseDefinitionTaskParams source);

  @Mapping(target = "existingLBs", ignore = true)
  @Mapping(target = "primaryCluster", ignore = true)
  @Mapping(target = "TServers", ignore = true)
  @Mapping(target = "readOnlyClusters", ignore = true)
  @Mapping(target = "addOnClusters", ignore = true)
  @Mapping(target = "nonPrimaryClusters", ignore = true)
  public FinalizeUpgradeParams toFinalizeUpgradeParams(UniverseDefinitionTaskParams source);

  @Mapping(target = "existingLBs", ignore = true)
  @Mapping(target = "primaryCluster", ignore = true)
  @Mapping(target = "TServers", ignore = true)
  @Mapping(target = "readOnlyClusters", ignore = true)
  @Mapping(target = "addOnClusters", ignore = true)
  @Mapping(target = "nonPrimaryClusters", ignore = true)
  public ThirdpartySoftwareUpgradeParams toThirdpartySoftwareUpgradeParams(
      UniverseDefinitionTaskParams source);

  @Mapping(target = "existingLBs", ignore = true)
  @Mapping(target = "primaryCluster", ignore = true)
  @Mapping(target = "TServers", ignore = true)
  @Mapping(target = "readOnlyClusters", ignore = true)
  @Mapping(target = "addOnClusters", ignore = true)
  @Mapping(target = "nonPrimaryClusters", ignore = true)
  public UpgradeTaskParams toUpgradeTaskParams(UniverseDefinitionTaskParams source);

  @Mapping(target = "existingLBs", ignore = true)
  @Mapping(target = "primaryCluster", ignore = true)
  @Mapping(target = "TServers", ignore = true)
  @Mapping(target = "readOnlyClusters", ignore = true)
  @Mapping(target = "addOnClusters", ignore = true)
  @Mapping(target = "nonPrimaryClusters", ignore = true)
  public RestartTaskParams toRestartTaskParams(UniverseDefinitionTaskParams source);

  @Mapping(target = "existingLBs", ignore = true)
  @Mapping(target = "primaryCluster", ignore = true)
  @Mapping(target = "TServers", ignore = true)
  @Mapping(target = "readOnlyClusters", ignore = true)
  @Mapping(target = "addOnClusters", ignore = true)
  @Mapping(target = "nonPrimaryClusters", ignore = true)
  public SystemdUpgradeParams toSystemdUpgradeParams(UniverseDefinitionTaskParams source);

  @Mapping(target = "existingLBs", ignore = true)
  @Mapping(target = "primaryCluster", ignore = true)
  @Mapping(target = "TServers", ignore = true)
  @Mapping(target = "readOnlyClusters", ignore = true)
  @Mapping(target = "addOnClusters", ignore = true)
  @Mapping(target = "nonPrimaryClusters", ignore = true)
  public TlsToggleParams toTlsToggleParams(UniverseDefinitionTaskParams source);

  @Mapping(target = "existingLBs", ignore = true)
  @Mapping(target = "primaryCluster", ignore = true)
  @Mapping(target = "TServers", ignore = true)
  @Mapping(target = "readOnlyClusters", ignore = true)
  @Mapping(target = "addOnClusters", ignore = true)
  @Mapping(target = "nonPrimaryClusters", ignore = true)
  public CertsRotateParams toCertsRotateParams(UniverseDefinitionTaskParams source);

  @Mapping(target = "spec", source = ".")
  UniverseCreateSpec toV2UniverseCreateSpec(UniverseDefinitionTaskParams v1UniverseTaskParams);

  @Mapping(target = "overridePrebuiltAmiDbVersion", source = "overridePrebuiltAmiDBVersion")
  @Mapping(target = "encryptionAtRestSpec", source = "encryptionAtRestConfig")
  @Mapping(target = "encryptionInTransitSpec", source = ".")
  @Mapping(target = "networkingSpec.communicationPorts", source = "communicationPorts")
  @Mapping(target = "ysql", source = ".")
  @Mapping(target = "ycql", source = ".")
  UniverseSpec toV2UniverseSpec(UniverseDefinitionTaskParams v1UniverseTaskParams);

  @InheritInverseConfiguration(name = "toV2UniverseSpec")
  UniverseDefinitionTaskParams toV1UniverseDefinitionTaskParams(UniverseSpec universeSpec);

  // This mapping is overridden in the Decorator, and only that version is used.
  @InheritInverseConfiguration(name = "toV2UniverseCreateSpec")
  UniverseDefinitionTaskParams toV1UniverseDefinitionTaskParamsFromCreateSpec(
      UniverseCreateSpec universeCreateSpec);

  UniverseDefinitionTaskParams toV1UniverseDefinitionTaskParamsFromEditSpec(
      UniverseEditSpec universeEditSpec,
      @MappingTarget UniverseDefinitionTaskParams v1UniverseDefinitionTaskParams);

  @Mapping(target = "universeUuid", source = "universeUUID")
  @Mapping(target = "updatingTaskUuid", source = "updatingTaskUUID")
  @Mapping(target = "encryptionAtRestInfo", source = "encryptionAtRestConfig")
  UniverseInfo toV2UniverseInfo(UniverseDefinitionTaskParams v1UniverseTaskParams);

  // below methods are used implicitly to generate other mapping

  @ValueMappings({
    @ValueMapping(target = "X86_64", source = "x86_64"),
    @ValueMapping(target = "AARCH64", source = "aarch64")
  })
  UniverseCreateSpec.ArchEnum toV2CreateSpecArch(Architecture arch);

  @InheritInverseConfiguration(name = "toV2CreateSpecArch")
  Architecture toV1Arch(UniverseCreateSpec.ArchEnum arch);

  @ValueMappings({
    @ValueMapping(target = "X86_64", source = "x86_64"),
    @ValueMapping(target = "AARCH64", source = "aarch64")
  })
  UniverseInfo.ArchEnum toV2UniverseInfoArch(Architecture arch);

  @InheritInverseConfiguration(name = "toV2UniverseInfoArch")
  Architecture toV1ArchFromInfo(UniverseInfo.ArchEnum arch);

  @Mapping(target = "kmsConfigUuid", source = "kmsConfigUUID")
  EncryptionAtRestSpec toV2EncryptionAtRestSpec(EncryptionAtRestConfig v1EncryptionAtRestConfig);

  @InheritInverseConfiguration
  @Mapping(target = "type", constant = "DATA_KEY")
  @Mapping(
      target = "opType",
      expression =
          "java(v2EncryptionAtRestSpec.getKmsConfigUuid() != null ?"
              + " com.yugabyte.yw.forms.EncryptionAtRestConfig.OpType.ENABLE :"
              + " com.yugabyte.yw.forms.EncryptionAtRestConfig.OpType.UNDEFINED)")
  @BeanMapping(nullValueMappingStrategy = NullValueMappingStrategy.RETURN_DEFAULT)
  EncryptionAtRestConfig toV1EncryptionAtRestConfig(EncryptionAtRestSpec v2EncryptionAtRestSpec);

  @Mapping(target = "encryptionAtRestStatus", source = "encryptionAtRestEnabled")
  EncryptionAtRestInfo toV2EncryptionAtRestInfo(EncryptionAtRestConfig v1EncryptionAtRestConfig);

  // copy EIT from primary cluster to top-level in v2
  default EncryptionInTransitSpec toV2EncryptionInTransitSpec(
      UniverseDefinitionTaskParams universeDetails) {
    UserIntent primaryUserIntent = universeDetails.getPrimaryCluster().userIntent;
    return new EncryptionInTransitSpec()
        .enableNodeToNodeEncrypt(primaryUserIntent.enableNodeToNodeEncrypt)
        .enableClientToNodeEncrypt(primaryUserIntent.enableClientToNodeEncrypt)
        .rootCa(universeDetails.rootCA)
        .clientRootCa(universeDetails.getClientRootCA());
  }

  default YSQLSpec toV2YsqlSpec(UniverseDefinitionTaskParams universeDetails) {
    UserIntent primaryUserIntent = universeDetails.getPrimaryCluster().userIntent;
    return new YSQLSpec()
        .enable(primaryUserIntent.enableYSQL)
        .enableAuth(primaryUserIntent.enableYSQLAuth)
        .password(primaryUserIntent.ysqlPassword);
  }

  default YCQLSpec toV2YcqlSpec(UniverseDefinitionTaskParams universeDetails) {
    UserIntent primaryUserIntent = universeDetails.getPrimaryCluster().userIntent;
    return new YCQLSpec()
        .enable(primaryUserIntent.enableYCQL)
        .enableAuth(primaryUserIntent.enableYCQLAuth)
        .password(primaryUserIntent.ycqlPassword);
  }

  String taskTypeEnumString(TaskType taskType);

  @ValueMappings({
    @ValueMapping(target = "READY", source = "Ready"),
    @ValueMapping(target = "UPGRADING", source = "Upgrading"),
    @ValueMapping(target = "UPGRADEFAILED", source = "UpgradeFailed"),
    @ValueMapping(target = "PREFINALIZE", source = "PreFinalize"),
    @ValueMapping(target = "FINALIZING", source = "Finalizing"),
    @ValueMapping(target = "FINALIZEFAILED", source = "FinalizeFailed"),
    @ValueMapping(target = "ROLLINGBACK", source = "RollingBack"),
    @ValueMapping(target = "ROLLBACKFAILED", source = "RollbackFailed")
  })
  UniverseInfo.SoftwareUpgradeStateEnum mapSoftwareUpgradeState(
      UniverseDefinitionTaskParams.SoftwareUpgradeState state);

  @ValueMappings({
    @ValueMapping(target = "NONE", source = "None"),
    @ValueMapping(target = "TOSTART", source = "ToStart"),
    @ValueMapping(target = "CONFIGURED", source = "Configured"),
    @ValueMapping(target = "TOSTOP", source = "ToStop")
  })
  MasterStateEnum mapMasterState(MasterState state);

  @Mapping(target = "disksAreMountedByUuid", source = "disksAreMountedByUUID")
  NodeDetails toV2NodeDetails(com.yugabyte.yw.models.helpers.NodeDetails v1NodeDetails);

  @Mapping(target = "assignPublicIp", source = "assignPublicIP")
  @Mapping(target = "instanceType", source = "instance_type")
  @Mapping(target = "mountRoots", source = "mount_roots")
  @Mapping(target = "privateDns", source = "private_dns")
  @Mapping(target = "privateIp", source = "private_ip")
  @Mapping(target = "publicDns", source = "public_dns")
  @Mapping(target = "publicIp", source = "public_ip")
  @Mapping(target = "rootVolume", source = "root_volume")
  @Mapping(target = "secondaryPrivateIp", source = "secondary_private_ip")
  @Mapping(target = "secondarySubnetId", source = "secondary_subnet_id")
  @Mapping(target = "subnetId", source = "subnet_id")
  CloudSpecificInfo toV2CloudSpecificInfo(
      com.yugabyte.yw.models.helpers.CloudSpecificInfo cloudSpecificInfo);

  @ValueMappings({
    @ValueMapping(target = "TOBEADDED", source = "ToBeAdded"),
    @ValueMapping(target = "INSTANCECREATED", source = "InstanceCreated"),
    @ValueMapping(target = "SERVERSETUP", source = "ServerSetup"),
    @ValueMapping(target = "TOJOINCLUSTER", source = "ToJoinCluster"),
    @ValueMapping(target = "REPROVISIONING", source = "Reprovisioning"),
    @ValueMapping(target = "PROVISIONED", source = "Provisioned"),
    @ValueMapping(target = "SOFTWAREINSTALLED", source = "SoftwareInstalled"),
    @ValueMapping(target = "UPGRADESOFTWARE", source = "UpgradeSoftware"),
    @ValueMapping(target = "ROLLBACKUPGRADE", source = "RollbackUpgrade"),
    @ValueMapping(target = "FINALIZEUPGRADE", source = "FinalizeUpgrade"),
    @ValueMapping(target = "UPDATEGFLAGS", source = "UpdateGFlags"),
    @ValueMapping(target = "LIVE", source = "Live"),
    @ValueMapping(target = "STOPPING", source = "Stopping"),
    @ValueMapping(target = "STARTING", source = "Starting"),
    @ValueMapping(target = "STOPPED", source = "Stopped"),
    @ValueMapping(target = "UNREACHABLE", source = "Unreachable"),
    @ValueMapping(target = "METRICSUNAVAILABLE", source = "MetricsUnavailable"),
    @ValueMapping(target = "TOBEREMOVED", source = "ToBeRemoved"),
    @ValueMapping(target = "REMOVING", source = "Removing"),
    @ValueMapping(target = "REMOVED", source = "Removed"),
    @ValueMapping(target = "ADDING", source = "Adding"),
    @ValueMapping(target = "BEINGDECOMMISSIONED", source = "BeingDecommissioned"),
    @ValueMapping(target = "DECOMMISSIONED", source = "Decommissioned"),
    @ValueMapping(target = "UPDATECERT", source = "UpdateCert"),
    @ValueMapping(target = "TOGGLETLS", source = "ToggleTls"),
    @ValueMapping(target = "CONFIGUREDBAPIS", source = "ConfigureDBApis"),
    @ValueMapping(target = "RESIZING", source = "Resizing"),
    @ValueMapping(target = "SYSTEMDUPGRADE", source = "SystemdUpgrade"),
    @ValueMapping(target = "TERMINATING", source = "Terminating"),
    @ValueMapping(target = "TERMINATED", source = "Terminated"),
    @ValueMapping(target = "REBOOTING", source = "Rebooting"),
    @ValueMapping(target = "HARDREBOOTING", source = "HardRebooting"),
    @ValueMapping(target = "VMIMAGEUPGRADE", source = "VMImageUpgrade")
  })
  NodeDetails.StateEnum toV2NodeState(
      com.yugabyte.yw.models.helpers.NodeDetails.NodeState v1NodeState);

  default OffsetDateTime toOffsetDateTime(Date date) {
    if (date == null) {
      return null;
    }
    return date.toInstant().atOffset(ZoneOffset.UTC);
  }
}
