package api.v2.mappers;

import api.v2.models.UniverseSoftwareUpgradeStart;
import com.yugabyte.yw.forms.SoftwareUpgradeParams;
import com.yugabyte.yw.models.Release;
import org.mapstruct.Mapper;
import org.mapstruct.Mapping;
import org.mapstruct.MappingTarget;
import org.mapstruct.factory.Mappers;

@Mapper
public interface UniverseSoftwareUpgradeStartMapper {
  UniverseSoftwareUpgradeStartMapper INSTANCE =
      Mappers.getMapper(UniverseSoftwareUpgradeStartMapper.class);

  @Mapping(target = "sleepAfterTServerRestartMillis", source = "sleepAfterTserverRestartMillis")
  @Mapping(target = "ybSoftwareVersion", source = "source")
  SoftwareUpgradeParams copyToV1SoftwareUpgradeParams(
      UniverseSoftwareUpgradeStart source, @MappingTarget SoftwareUpgradeParams target);

  // Map ybRelease from the release uuid to the version string
  default String updaterYbSoftwareVersion(UniverseSoftwareUpgradeStart source) {
    Release release = Release.getOrBadRequest(source.getYugabyteRelease());
    return release.getVersion();
  }
}
