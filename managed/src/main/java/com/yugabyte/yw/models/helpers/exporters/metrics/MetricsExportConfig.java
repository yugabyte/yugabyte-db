package com.yugabyte.yw.models.helpers.exporters.metrics;

import static io.swagger.annotations.ApiModelProperty.AccessMode.READ_WRITE;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.yugabyte.yw.models.helpers.MetricCollectionLevel;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.EnumSet;
import java.util.List;
import java.util.Set;
import javax.validation.constraints.NotNull;
import lombok.Data;
import org.apache.commons.collections4.CollectionUtils;

@Data
@ApiModel(description = "Metrics Export Configuration")
public class MetricsExportConfig {

  @ApiModelProperty(
      value = "Scrape interval in seconds. Applied on all scrape jobs commonly.",
      accessMode = READ_WRITE,
      example = "30")
  private Integer scrapeIntervalSeconds = 30;

  @ApiModelProperty(
      value = "Scrape timeout in seconds. Applied on all scrape jobs commonly.",
      accessMode = READ_WRITE,
      example = "20")
  private Integer scrapeTimeoutSeconds = 20;

  @ApiModelProperty(
      value = "The level of metrics collection. Allowed values are: ALL, NORMAL, MINIMAL, OFF",
      accessMode = READ_WRITE,
      example = "NORMAL")
  private MetricCollectionLevel collectionLevel = MetricCollectionLevel.NORMAL;

  @ApiModelProperty(
      value =
          "Set of target types to include in scrape configuration. If not specified, all supported"
              + " target types will be included.",
      accessMode = READ_WRITE,
      example =
          "[\"MASTER_EXPORT\", \"TSERVER_EXPORT\", \"YSQL_EXPORT\", \"CQL_EXPORT\","
              + " \"NODE_EXPORT\", \"NODE_AGENT_EXPORT\", \"OTEL_EXPORT\"]")
  private Set<ScrapeConfigTargetType> scrapeConfigTargets =
      EnumSet.allOf(ScrapeConfigTargetType.class);

  @NotNull
  @ApiModelProperty(
      value =
          "List of universe metrics exporter configurations. If empty, no metrics will be sent"
              + " anywhere.",
      accessMode = READ_WRITE)
  private List<UniverseMetricsExporterConfig> universeMetricsExporterConfig;

  @JsonIgnore
  public boolean isExportActive() {
    return universeMetricsExporterConfig != null
        && CollectionUtils.isNotEmpty(universeMetricsExporterConfig);
  }
}
