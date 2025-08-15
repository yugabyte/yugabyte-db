package com.yugabyte.yw.forms;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.databind.annotation.JsonDeserialize;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.exporters.metrics.MetricsExportConfig;
import lombok.Data;
import org.apache.commons.collections4.CollectionUtils;

@Data
@JsonIgnoreProperties(ignoreUnknown = true)
@JsonDeserialize(converter = MetricsExportConfigParams.Converter.class)
public class MetricsExportConfigParams extends UpgradeTaskParams {

  public MetricsExportConfig metricsExportConfig;
  public boolean installOtelCollector;

  @Override
  public boolean isKubernetesUpgradeSupported() {
    return false;
  }

  @Override
  public void verifyParams(Universe universe, boolean isFirstTry) {
    super.verifyParams(universe, isFirstTry);
    boolean exportEnabled =
        metricsExportConfig.isExportActive()
            && CollectionUtils.isNotEmpty(metricsExportConfig.getUniverseMetricsExporterConfig());
    if (exportEnabled
        && !universe.getUniverseDetails().otelCollectorEnabled
        && !installOtelCollector) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Universe "
              + universe.getUniverseUUID()
              + " does not have OpenTelemetry Collector installed and task params has"
              + " installOtelCollector=false - can't configure metrics export for the universe");
    }
    if (exportEnabled
        && universe
            .getUniverseDetails()
            .getPrimaryCluster()
            .userIntent
            .providerType
            .equals(CloudType.kubernetes)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Metrics export is not yet supported for kubernetes based universes.");
    }
  }

  public static class Converter extends BaseConverter<MetricsExportConfigParams> {}
}
