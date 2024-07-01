package api.v2.mappers;

import api.v2.models.UniverseSoftwareFinalizeImpactedXCluster;
import api.v2.models.UniverseSoftwareUpgradeFinalize;
import com.yugabyte.yw.forms.FinalizeUpgradeParams;
import com.yugabyte.yw.models.extended.FinalizeUpgradeInfoResponse;
import org.mapstruct.Mapper;
import org.mapstruct.Mapping;
import org.mapstruct.MappingTarget;
import org.mapstruct.factory.Mappers;

@Mapper
public interface UniverseSoftwareFinalizeMapper {
  UniverseSoftwareFinalizeMapper INSTANCE = Mappers.getMapper(UniverseSoftwareFinalizeMapper.class);

  FinalizeUpgradeParams copyToV1FinalizeUpgradeParams(
      UniverseSoftwareUpgradeFinalize source, @MappingTarget FinalizeUpgradeParams target);

  @Mapping(source = "universeUUID", target = "universeUuid")
  @Mapping(source = "ybSoftwareVersion", target = "universeVersion")
  UniverseSoftwareFinalizeImpactedXCluster toV2UniverseSoftwareFinalizeImpactedXCluster(
      FinalizeUpgradeInfoResponse.ImpactedXClusterConnectedUniverse
          v1ImpactedXClusterConnectedUniverse);
}
