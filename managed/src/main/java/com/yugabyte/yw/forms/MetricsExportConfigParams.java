package com.yugabyte.yw.forms;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.databind.annotation.JsonDeserialize;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.MetricCollectionLevel;
import com.yugabyte.yw.models.helpers.exporters.metrics.MetricsExportConfig;
import com.yugabyte.yw.models.helpers.exporters.metrics.ScrapeConfigTargetType;
import java.util.EnumSet;
import java.util.Set;
import lombok.Data;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;

@Slf4j
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

    if (CollectionUtils.isEmpty(metricsExportConfig.getScrapeConfigTargets())) {
      Set<ScrapeConfigTargetType> scrapeConfigTargets = EnumSet.allOf(ScrapeConfigTargetType.class);
      log.warn(
          "No scrape config targets specified, using default scrape config targets: '{}'",
          scrapeConfigTargets);
      metricsExportConfig.setScrapeConfigTargets(scrapeConfigTargets);
    }

    if (MetricCollectionLevel.OFF.equals(metricsExportConfig.getCollectionLevel())) {
      String errorMessage =
          "Metrics collection level cannot be set to OFF during metrics export configuration for"
              + " universe "
              + universe.getUniverseUUID();
      log.error(errorMessage);
      throw new PlatformServiceException(BAD_REQUEST, errorMessage);
    }

    if (metricsExportConfig.getScrapeIntervalSeconds() <= 0) {
      String errorMessage =
          "Scrape interval seconds cannot be set to <=0 during metrics export configuration for"
              + " universe "
              + universe.getUniverseUUID();
      log.error(errorMessage);
      throw new PlatformServiceException(BAD_REQUEST, errorMessage);
    }

    if (metricsExportConfig.getScrapeTimeoutSeconds() <= 0) {
      String errorMessage =
          "Scrape timeout seconds cannot be set to <=0 during metrics export configuration for"
              + " universe "
              + universe.getUniverseUUID();
      log.error(errorMessage);
      throw new PlatformServiceException(BAD_REQUEST, errorMessage);
    }

    if (metricsExportConfig.getScrapeIntervalSeconds()
        < metricsExportConfig.getScrapeTimeoutSeconds()) {
      String errorMessage =
          "Scrape interval seconds cannot be less than scrape timeout seconds during metrics export"
              + " configuration for universe "
              + universe.getUniverseUUID();
      log.error(errorMessage);
      throw new PlatformServiceException(BAD_REQUEST, errorMessage);
    }
  }

  public static class Converter extends BaseConverter<MetricsExportConfigParams> {}
}
