// Copyright (c) YugabyteDB, Inc.

package api.v2.mappers;

import api.v2.models.SupportBundle;
import api.v2.models.SupportBundleInfo;
import api.v2.models.SupportBundleSpec;
import com.yugabyte.yw.common.SupportBundleUtil;
import com.yugabyte.yw.models.SupportBundleV2;
import com.yugabyte.yw.models.SupportBundleV2StatusType;
import java.time.OffsetDateTime;
import java.time.ZoneOffset;
import org.mapstruct.Context;
import org.mapstruct.Mapper;
import org.mapstruct.Mapping;
import org.mapstruct.factory.Mappers;

/**
 * Maps the {@link SupportBundleV2} entity onto the v2 API {@code spec}/{@code info} pair.
 *
 * <p>The bundle's collection settings live in the {@code bundleDetails} JSON column, so the spec is
 * assembled from both the entity and that payload.
 */
@Mapper(
    config = CentralConfig.class,
    uses = {
      DateTimeMapper.class,
      SupportBundleComponentSpecMapper.class,
      SupportBundleEnumMapper.class
    })
public interface SupportBundleMapper {

  SupportBundleMapper INSTANCE = Mappers.getMapper(SupportBundleMapper.class);

  @Mapping(target = "spec", source = ".")
  @Mapping(target = "info", source = ".")
  SupportBundle toApi(SupportBundleV2 source, @Context int retentionDays);

  @Mapping(target = "components", source = "bundleDetails.components")
  @Mapping(target = "nodeNames", source = "bundleDetails.nodeNames")
  @Mapping(target = "filesComponentSpecs", source = "bundleDetails.filesComponentSpecs")
  @Mapping(target = "bashComponentSpecs", source = "bundleDetails.bashComponentSpecs")
  @Mapping(target = "ysqlComponentSpecs", source = "bundleDetails.ysqlComponentSpecs")
  @Mapping(target = "ycqlComponentSpecs", source = "bundleDetails.ycqlComponentSpecs")
  @Mapping(target = "ybAdminComponentSpecs", source = "bundleDetails.ybAdminComponentSpecs")
  @Mapping(target = "ybaComponentSpecs", source = "bundleDetails.ybaComponentSpecs")
  @Mapping(target = "maxCoreFileSize", source = "bundleDetails.maxCoreFileSize")
  @Mapping(target = "maxNumRecentCores", source = "bundleDetails.maxNumRecentCores")
  @Mapping(target = "promDumpStartDate", source = "bundleDetails.promDumpStartDate")
  @Mapping(target = "promDumpEndDate", source = "bundleDetails.promDumpEndDate")
  @Mapping(target = "promMetricsFormat", source = "bundleDetails.promMetricsFormat")
  @Mapping(target = "promMetricsStepSec", source = "bundleDetails.promMetricsStepSec")
  @Mapping(target = "prometheusMetricsTypes", source = "bundleDetails.prometheusMetricsTypes")
  @Mapping(target = "paDumpStartDate", source = "bundleDetails.paDumpStartDate")
  @Mapping(target = "paDumpEndDate", source = "bundleDetails.paDumpEndDate")
  @Mapping(target = "paMetricsFormat", source = "bundleDetails.paMetricsFormat")
  SupportBundleSpec toSupportBundleSpec(SupportBundleV2 source);

  @Mapping(target = "uuid", source = "bundleUUID")
  @Mapping(target = "scopeUuid", source = "scopeUUID")
  @Mapping(target = "customerUuid", source = "customerUUID")
  @Mapping(target = "expirationDate", expression = "java(expirationDate(source, retentionDays))")
  SupportBundleInfo toSupportBundleInfo(SupportBundleV2 source, @Context int retentionDays);

  /**
   * A bundle only occupies disk, and therefore only expires, once collection has succeeded. Every
   * row has a creation date from the moment it is requested, so the status is what gates this.
   */
  default OffsetDateTime expirationDate(SupportBundleV2 source, int retentionDays) {
    if (source.getStatus() != SupportBundleV2StatusType.Success) {
      return null;
    }
    return new SupportBundleUtil()
        .getDateNDaysAfter(source.getCreationDate(), retentionDays)
        .toInstant()
        .atOffset(ZoneOffset.UTC);
  }
}
