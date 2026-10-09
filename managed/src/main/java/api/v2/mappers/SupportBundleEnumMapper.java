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
import org.mapstruct.ValueMapping;
import org.mapstruct.ValueMappings;
import org.mapstruct.factory.Mappers;

/**
 * Support bundle enum mappings between the internal types and the generated v2 API models.
 *
 * <p>Constants are matched by name. MapStruct fails the build when a source constant has no
 * counterpart, so adding a value to any of these enums without also adding it to the OpenAPI schema
 * is a compile error rather than a runtime surprise.
 *
 * <p>SupportBundleStatus and PrometheusMetricsType carry x-enum-varnames in the OpenAPI schema (the
 * generated Go constants are package-scoped and the bare values collided), so their names no longer
 * line up with the internal enums and the mappings have to be spelled out.
 */
@Mapper(unmappedSourcePolicy = ReportingPolicy.ERROR)
public interface SupportBundleEnumMapper {

  SupportBundleEnumMapper INSTANCE = Mappers.getMapper(SupportBundleEnumMapper.class);

  SupportBundleComponentType toComponentType(ComponentType source);

  ComponentType toV1ComponentType(SupportBundleComponentType source);

  @ValueMappings({
    @ValueMapping(target = "SupportBundleRunning", source = "Running"),
    @ValueMapping(target = "SupportBundleSuccess", source = "Success"),
    @ValueMapping(target = "SupportBundleFailed", source = "Failed"),
    @ValueMapping(target = "SupportBundleAborted", source = "Aborted")
  })
  SupportBundleStatus toSupportBundleStatus(SupportBundleV2StatusType source);

  @ValueMappings({
    @ValueMapping(target = "PROMETHEUS_METRICS_MASTER_EXPORT", source = "MASTER_EXPORT"),
    @ValueMapping(target = "PROMETHEUS_METRICS_NODE_EXPORT", source = "NODE_EXPORT"),
    @ValueMapping(target = "PROMETHEUS_METRICS_PLATFORM", source = "PLATFORM"),
    @ValueMapping(target = "PROMETHEUS_METRICS_PROMETHEUS", source = "PROMETHEUS"),
    @ValueMapping(target = "PROMETHEUS_METRICS_TSERVER_EXPORT", source = "TSERVER_EXPORT"),
    @ValueMapping(target = "PROMETHEUS_METRICS_CQL_EXPORT", source = "CQL_EXPORT"),
    @ValueMapping(target = "PROMETHEUS_METRICS_YSQL_EXPORT", source = "YSQL_EXPORT")
  })
  PrometheusMetricsType toPrometheusMetricsType(
      com.yugabyte.yw.models.helpers.BundleDetails.PrometheusMetricsType source);

  @ValueMappings({
    @ValueMapping(target = "MASTER_EXPORT", source = "PROMETHEUS_METRICS_MASTER_EXPORT"),
    @ValueMapping(target = "NODE_EXPORT", source = "PROMETHEUS_METRICS_NODE_EXPORT"),
    @ValueMapping(target = "PLATFORM", source = "PROMETHEUS_METRICS_PLATFORM"),
    @ValueMapping(target = "PROMETHEUS", source = "PROMETHEUS_METRICS_PROMETHEUS"),
    @ValueMapping(target = "TSERVER_EXPORT", source = "PROMETHEUS_METRICS_TSERVER_EXPORT"),
    @ValueMapping(target = "CQL_EXPORT", source = "PROMETHEUS_METRICS_CQL_EXPORT"),
    @ValueMapping(target = "YSQL_EXPORT", source = "PROMETHEUS_METRICS_YSQL_EXPORT")
  })
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
