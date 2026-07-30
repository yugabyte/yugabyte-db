// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.forms;

import com.fasterxml.jackson.annotation.JsonFormat;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.helpers.BundleDetails.ComponentType;
import com.yugabyte.yw.models.helpers.BundleDetails.PromExportType;
import com.yugabyte.yw.models.helpers.BundleDetails.PrometheusMetricsFormat;
import com.yugabyte.yw.models.helpers.BundleDetails.PrometheusMetricsType;
import io.swagger.annotations.ApiModel;
import io.swagger.annotations.ApiModelProperty;
import java.util.Date;
import java.util.EnumSet;
import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.TimeUnit;

@ApiModel(description = "Support bundle form metadata")
public class SupportBundleFormData {

  @ApiModelProperty(
      value = "Start date to filter logs from",
      required = true,
      example = "2022-12-12T13:07:18Z")
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  public Date startDate;

  @ApiModelProperty(
      value = "End date to filter logs till",
      required = true,
      example = "2022-12-12T13:07:18Z")
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  public Date endDate;

  @ApiModelProperty(
      value = "List of components to be included in the support bundle",
      required = true)
  public EnumSet<ComponentType> components;

  @ApiModelProperty(
      value = "Max number of the most recent cores to collect (if any)",
      required = false)
  public int maxNumRecentCores = 1;

  @ApiModelProperty(
      value = "Max size in bytes of the recent collected cores (if any)",
      required = false)
  public long maxCoreFileSize = 25000000000L;

  @ApiModelProperty(
      value = "Start date to filter prometheus metrics from",
      required = false,
      example = "2022-12-12T13:07:18Z")
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  public Date promDumpStartDate;

  @ApiModelProperty(
      value = "End date to filter prometheus metrics till",
      required = false,
      example = "2022-12-12T13:07:18Z")
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  public Date promDumpEndDate;

  @ApiModelProperty(
      value = "List of exports to be included in the prometheus dump",
      required = false)
  public EnumSet<PrometheusMetricsType> prometheusMetricsTypes =
      EnumSet.noneOf(PrometheusMetricsType.class);

  @ApiModelProperty(
      value = "Map of query names to custom PromQL queries to collect in promdump",
      required = false)
  public Map<String, String> promQueries = new HashMap<>();

  @ApiModelProperty(
      value =
          "How to export Prometheus metrics: PROMQL (query_range) or REMOTE_READ. Default PROMQL"
              + " for backward compatibility.")
  public PromExportType promExportType = PromExportType.PROMQL;

  @ApiModelProperty(
      value =
          "When promExportType is REMOTE_READ, format for remote read export: PROMQL_JSON or"
              + " PROM_CHUNK. Default PROMQL_JSON for backward compatibility.")
  public PrometheusMetricsFormat promMetricsFormat = PrometheusMetricsFormat.PROMQL_JSON;

  @ApiModelProperty(
      value =
          "When promExportType is REMOTE_READ, whether to downsample raw data points by"
              + " yb.support_bundle.step_prom_dump_secs or stepPromDumpSecs."
              + " Ignored for PROMQL (always downsampled). Default true.")
  public boolean promDumpDownSample = true;

  @ApiModelProperty(
      value =
          "Metrics downsample step (in seconds). Overrides global default."
              + " Use with batchDurationPromDumpMins to get longer historical trends while keeping"
              + " the same number of data points",
      required = false)
  public Integer stepPromDumpSecs;

  @ApiModelProperty(
      value =
          "Batch duration for the prometheus dump (in minutes). Overrides global default."
              + " Use with stepPromDumpSecs to get longer historical trends while keeping"
              + " the same number of data points",
      required = false)
  public Integer batchDurationPromDumpMins;

  @ApiModelProperty(
      value =
          "Specifies if Postgres audit logs should be filtered out when collecting universe logs.")
  public boolean filterPgAuditLogs = false;

  @ApiModelProperty(
      value = "Start date to filter Perf Advisor data",
      required = false,
      example = "2022-12-12T13:07:18Z")
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  public Date paDumpStartDate;

  @ApiModelProperty(
      value = "End date to filter Perf Advisor data",
      required = false,
      example = "2022-12-12T13:07:18Z")
  @JsonFormat(shape = JsonFormat.Shape.STRING, pattern = "yyyy-MM-dd'T'HH:mm:ss'Z'")
  public Date paDumpEndDate;

  @ApiModelProperty(value = "Specifies metrics format.")
  public PrometheusMetricsFormat paMetricsFormat = PrometheusMetricsFormat.PROM_CHUNK;

  public void resolveDefaultDates(RuntimeConfGetter confGetter) {
    if (endDate == null) {
      return;
    }
    if (promDumpStartDate == null && promDumpEndDate == null) {
      int defaultPromDumpRange =
          confGetter.getGlobalConf(GlobalConfKeys.supportBundleDefaultPromDumpRange);
      promDumpEndDate = endDate;
      promDumpStartDate =
          new Date(endDate.getTime() - TimeUnit.MINUTES.toMillis(defaultPromDumpRange));
    }
    if (paDumpStartDate == null && paDumpEndDate == null) {
      int defaultPaDumpRange =
          confGetter.getGlobalConf(GlobalConfKeys.supportBundleDefaultPaDumpRange);
      paDumpEndDate = endDate;
      paDumpStartDate = new Date(endDate.getTime() - TimeUnit.MINUTES.toMillis(defaultPaDumpRange));
    }
  }
}
