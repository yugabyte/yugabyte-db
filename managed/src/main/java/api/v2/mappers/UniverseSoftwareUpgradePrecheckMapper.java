package api.v2.mappers;

import api.v2.models.UniverseSoftwareUpgradePrecheckReq;
import api.v2.models.UniverseSoftwareUpgradePrecheckResp;
import com.yugabyte.yw.models.extended.SoftwareUpgradeInfoRequest;
import com.yugabyte.yw.models.extended.SoftwareUpgradeInfoResponse;
import org.mapstruct.Mapper;
import org.mapstruct.factory.Mappers;

@Mapper
public interface UniverseSoftwareUpgradePrecheckMapper {
  UniverseSoftwareUpgradePrecheckMapper INSTANCE =
      Mappers.getMapper(UniverseSoftwareUpgradePrecheckMapper.class);

  UniverseSoftwareUpgradePrecheckResp toUniverseSoftwareUpgradePrecheckResp(
      SoftwareUpgradeInfoResponse source);

  SoftwareUpgradeInfoRequest toSoftwareUpgradeInfoRequest(
      UniverseSoftwareUpgradePrecheckReq source);
}
