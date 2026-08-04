package api.v2.mappers;

import api.v2.models.UniverseCertRotateSpec;
import com.yugabyte.yw.forms.CertsRotateParams;
import com.yugabyte.yw.forms.SoftwareUpgradeParams;
import org.mapstruct.Mapper;
import org.mapstruct.Mapping;
import org.mapstruct.MappingTarget;
import org.mapstruct.factory.Mappers;

@Mapper(config = CentralConfig.class)
public interface UniverseCertsRotateParamsMapper {
  UniverseCertsRotateParamsMapper INSTANCE =
      Mappers.getMapper(UniverseCertsRotateParamsMapper.class);

  @Mapping(target = "upgradeOption", source = "source")
  @Mapping(target = "rootAndClientRootCASame", source = "source")
  @Mapping(target = "sleepAfterTServerRestartMillis", source = "sleepAfterTserverRestartMillis")
  @Mapping(target = "rootCA", source = "rootCa")
  @Mapping(target = "clientRootCA", source = "clientRootCa")
  CertsRotateParams copyToV1CertsRotateParams(
      UniverseCertRotateSpec source, @MappingTarget CertsRotateParams target);

  default SoftwareUpgradeParams.UpgradeOption mapUpgradeOption(UniverseCertRotateSpec source) {
    return source.getRollingUpgrade()
        ? SoftwareUpgradeParams.UpgradeOption.ROLLING_UPGRADE
        : SoftwareUpgradeParams.UpgradeOption.NON_ROLLING_UPGRADE;
  }

  default boolean mapRootAndClientRootCASame(UniverseCertRotateSpec spec) {
    if (spec.getRootCa() == null) {
      return false;
    }
    return spec.getRootCa().equals(spec.getClientRootCa());
  }
}
