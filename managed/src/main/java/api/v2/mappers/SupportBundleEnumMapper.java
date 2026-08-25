// Copyright (c) YugabyteDB, Inc.

package api.v2.mappers;

import api.v2.models.PromExportType;
import api.v2.models.PrometheusMetricsFormat;
import api.v2.models.PrometheusMetricsType;
import api.v2.models.SupportBundleComponentType;
import api.v2.models.SupportBundleStatus;
import com.yugabyte.yw.models.SupportBundleV2StatusType;
import com.yugabyte.yw.models.helpers.BundleDetails.ComponentType;
import org.mapstruct.Mapper;
import org.mapstruct.ReportingPolicy;
import org.mapstruct.factory.Mappers;

/**
 * Support bundle enum mappings between the internal types and the generated v2 API models.
 *
 * <p>Constants are matched by name. MapStruct fails the build when a source constant has no
 * counterpart, so adding a value to any of these enums without also adding it to the OpenAPI schema
 * is a compile error rather than a runtime surprise.
 */
@Mapper(unmappedSourcePolicy = ReportingPolicy.ERROR)
public interface SupportBundleEnumMapper {

  SupportBundleEnumMapper INSTANCE = Mappers.getMapper(SupportBundleEnumMapper.class);

  SupportBundleComponentType toComponentType(ComponentType source);

  ComponentType toV1ComponentType(SupportBundleComponentType source);

  SupportBundleStatus toSupportBundleStatus(SupportBundleV2StatusType source);

  PrometheusMetricsType toPrometheusMetricsType(
      com.yugabyte.yw.models.helpers.BundleDetails.PrometheusMetricsType source);

  com.yugabyte.yw.models.helpers.BundleDetails.PrometheusMetricsType toV1PrometheusMetricsType(
      PrometheusMetricsType source);

  PrometheusMetricsFormat toPrometheusMetricsFormat(
      com.yugabyte.yw.models.helpers.BundleDetails.PrometheusMetricsFormat source);

  com.yugabyte.yw.models.helpers.BundleDetails.PrometheusMetricsFormat toV1PrometheusMetricsFormat(
      PrometheusMetricsFormat source);

  PromExportType toPromExportType(
      com.yugabyte.yw.models.helpers.BundleDetails.PromExportType source);

  com.yugabyte.yw.models.helpers.BundleDetails.PromExportType toV1PromExportType(
      PromExportType source);
}
