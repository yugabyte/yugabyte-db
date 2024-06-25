package api.v2.mappers;

import api.v2.models.UniverseSoftwareUpgradeStart;
import com.yugabyte.yw.forms.SoftwareUpgradeParams;
import org.mapstruct.Mapper;
import org.mapstruct.Mapping;
import org.mapstruct.MappingTarget;
import org.mapstruct.factory.Mappers;

@Mapper
public interface UniverseSoftwareUpgradeStartMapper {
  UniverseSoftwareUpgradeStartMapper INSTANCE =
      Mappers.getMapper(UniverseSoftwareUpgradeStartMapper.class);

  @Mapping(target = "sleepAfterTServerRestartMillis", source = "sleepAfterTserverRestartMillis")
  @Mapping(target = "ybSoftwareVersion", source = "version")
  SoftwareUpgradeParams copyToV1SoftwareUpgradeParams(
      UniverseSoftwareUpgradeStart source, @MappingTarget SoftwareUpgradeParams target);
}
