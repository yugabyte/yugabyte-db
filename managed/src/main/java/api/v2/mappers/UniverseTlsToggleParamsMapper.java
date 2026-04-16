package api.v2.mappers;

import api.v2.models.UniverseEditEncryptionInTransit;
import com.yugabyte.yw.forms.SoftwareUpgradeParams;
import com.yugabyte.yw.forms.TlsToggleParams;
import org.mapstruct.Mapper;
import org.mapstruct.Mapping;
import org.mapstruct.MappingTarget;
import org.mapstruct.factory.Mappers;

@Mapper(config = CentralConfig.class)
public interface UniverseTlsToggleParamsMapper {
  UniverseTlsToggleParamsMapper INSTANCE = Mappers.getMapper(UniverseTlsToggleParamsMapper.class);

  @Mapping(target = "upgradeOption", source = "source")
  @Mapping(target = "rootAndClientRootCASame", source = "source")
  @Mapping(target = "enableNodeToNodeEncrypt", source = "nodeToNode")
  @Mapping(target = "enableClientToNodeEncrypt", source = "clientToNode")
  @Mapping(target = "sleepAfterTServerRestartMillis", source = "sleepAfterTserverRestartMillis")
  @Mapping(target = "rootCA", source = "rootCa")
  @Mapping(target = "clientRootCA", source = "clientRootCa")
  TlsToggleParams copyToV1TlsToggleParams(
      UniverseEditEncryptionInTransit source, @MappingTarget TlsToggleParams target);

  default SoftwareUpgradeParams.UpgradeOption mapUpgradeOption(
      UniverseEditEncryptionInTransit source) {
    return source.getRollingUpgrade()
        ? SoftwareUpgradeParams.UpgradeOption.ROLLING_UPGRADE
        : SoftwareUpgradeParams.UpgradeOption.NON_ROLLING_UPGRADE;
  }

  default boolean mapRootAndClientRootCASame(UniverseEditEncryptionInTransit spec) {
    if (spec.getRootCa() == null) {
      return false;
    }
    return spec.getRootCa().equals(spec.getClientRootCa());
  }
}
