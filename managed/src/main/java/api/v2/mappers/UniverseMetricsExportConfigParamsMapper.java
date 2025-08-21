// Copyright (c) YugaByte, Inc.
package api.v2.mappers;

import api.v2.models.ConfigureMetricsExportSpec;
import com.yugabyte.yw.forms.MetricsExportConfigParams;
import org.mapstruct.Mapper;
import org.mapstruct.Mapping;
import org.mapstruct.MappingTarget;
import org.mapstruct.factory.Mappers;

@Mapper(config = CentralConfig.class)
public interface UniverseMetricsExportConfigParamsMapper {
  UniverseMetricsExportConfigParamsMapper INSTANCE =
      Mappers.getMapper(UniverseMetricsExportConfigParamsMapper.class);

  @Mapping(source = "installOtelCollector", target = "installOtelCollector")
  @Mapping(source = "metricsExportConfig", target = "metricsExportConfig")
  @Mapping(source = "sleepAfterTserverRestartMillis", target = "sleepAfterTServerRestartMillis")
  @Mapping(source = "sleepAfterMasterRestartMillis", target = "sleepAfterMasterRestartMillis")
  MetricsExportConfigParams copyToV1MetricsExportConfigParams(
      ConfigureMetricsExportSpec source, @MappingTarget MetricsExportConfigParams target);
}
