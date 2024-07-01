package api.v2.mappers;

import api.v2.models.UniverseSoftwareFinalizeImpactedXCluster;
import api.v2.models.UniverseSoftwareUpgradeFinalizeInfo;
import com.yugabyte.yw.models.extended.FinalizeUpgradeInfoResponse;
import org.mapstruct.Mapper;
import org.mapstruct.Mapping;
import org.mapstruct.factory.Mappers;

@Mapper
public interface UniverseSoftwareFinalizeRespMapper {
  UniverseSoftwareFinalizeRespMapper INSTANCE =
      Mappers.getMapper(UniverseSoftwareFinalizeRespMapper.class);

  @Mapping(source = "impactedXClusterConnectedUniverse", target = "impactedXclusters")
  UniverseSoftwareUpgradeFinalizeInfo toV2UniverseSoftwareFinalizeInfo(
      FinalizeUpgradeInfoResponse v1UniverseSoftwareFinalizeResp);

  @Mapping(source = "universeUUID", target = "universeUuid")
  @Mapping(source = "ybSoftwareVersion", target = "universeVersion")
  UniverseSoftwareFinalizeImpactedXCluster toV2UniverseSoftwareFinalizeImpactedXCluster(
      FinalizeUpgradeInfoResponse.ImpactedXClusterConnectedUniverse v1ImpactedXCluster);
}
