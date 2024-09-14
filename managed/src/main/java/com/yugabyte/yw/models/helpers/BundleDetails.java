package com.yugabyte.yw.models.helpers;

import com.fasterxml.jackson.annotation.JsonFormat;
import com.fasterxml.jackson.annotation.JsonIgnore;
import io.ebean.annotation.EnumValue;
import io.swagger.annotations.ApiModelProperty;
import java.util.Date;
import java.util.EnumSet;
import java.util.stream.Collectors;

public class BundleDetails {

  public enum ComponentLevel {
    NodeLevel,
    GlobalLevel;
  }

  public enum ComponentType {
    @EnumValue("UniverseLogs")
    UniverseLogs(ComponentLevel.NodeLevel),

    @EnumValue("OutputFiles")
    OutputFiles(ComponentLevel.NodeLevel),

    @EnumValue("ErrorFiles")
    ErrorFiles(ComponentLevel.NodeLevel),

    @EnumValue("CoreFiles")
    CoreFiles(ComponentLevel.NodeLevel),

    @EnumValue("GFlags")
    GFlags(ComponentLevel.NodeLevel),

    @EnumValue("Instance")
    Instance(ComponentLevel.NodeLevel),

    @EnumValue("ConsensusMeta")
    ConsensusMeta(ComponentLevel.NodeLevel),

    @EnumValue("TabletMeta")
    TabletMeta(ComponentLevel.NodeLevel),

    @EnumValue("YbcLogs")
    YbcLogs(ComponentLevel.NodeLevel),

    @EnumValue("K8sInfo")
    K8sInfo(ComponentLevel.GlobalLevel),

    @EnumValue("NodeAgent")
    NodeAgent(ComponentLevel.NodeLevel),

    @EnumValue("YbaMetadata")
    YbaMetadata(ComponentLevel.GlobalLevel),

    @EnumValue("PrometheusMetrics")
    PrometheusMetrics(ComponentLevel.GlobalLevel),

    // Add any new components above this component, as we want this to be the last collected
    // component to debug any issues with support bundle itself.
    @EnumValue("ApplicationLogs")
    ApplicationLogs(ComponentLevel.GlobalLevel);

    private final ComponentLevel componentLevel;

    ComponentType(ComponentLevel componentLevel) {
      this.componentLevel = componentLevel;
    }

    public ComponentLevel getComponentLevel() {
      return this.componentLevel;
    }

    public static boolean isValid(String type) {
      for (ComponentType t : ComponentType.values()) {
        if (t.name().equals(type)) {
          return true;
        }
      }

      return false;
    }
  }

  public enum PrometheusMetricsType {
    MASTER_EXPORT,
    NODE_EXPORT,
    PLATFORM,
    PROMETHEUS,
    TSERVER_EXPORT,
    CQL_EXPORT,
    YSQL_EXPORT;
  }

  public EnumSet<ComponentType> components;

  @ApiModelProperty(value = "Max number of most recent cores to collect (if any)", required = false)
  public int maxNumRecentCores;

  @ApiModelProperty(value = "Max size of the collected cores (if any)", required = false)
  public long maxCoreFileSize;

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
  public EnumSet<PrometheusMetricsType> prometheusMetricsTypes;

  public BundleDetails() {}

  public BundleDetails(
      EnumSet<ComponentType> components, int maxNumRecentCores, long maxCoreFileSize) {
    this.components = components;
    this.maxNumRecentCores = maxNumRecentCores;
    this.maxCoreFileSize = maxCoreFileSize;
  }

  public BundleDetails(
      EnumSet<ComponentType> components,
      int maxNumRecentCores,
      long maxCoreFileSize,
      Date promDumpStartDate,
      Date promDumpEnDate,
      EnumSet<PrometheusMetricsType> prometheusMetricsTypes) {
    this.components = components;
    this.maxNumRecentCores = maxNumRecentCores;
    this.maxCoreFileSize = maxCoreFileSize;
    this.promDumpStartDate = promDumpStartDate;
    this.promDumpEndDate = promDumpEnDate;
    this.prometheusMetricsTypes = prometheusMetricsTypes;
  }

  @JsonIgnore
  public EnumSet<ComponentType> getNodeLevelComponents() {
    return this.components.stream()
        .filter(ct -> ComponentLevel.NodeLevel.equals(ct.getComponentLevel()))
        .collect(Collectors.toCollection(() -> EnumSet.noneOf(ComponentType.class)));
  }

  @JsonIgnore
  public EnumSet<ComponentType> getGlobalLevelComponents() {
    return this.components.stream()
        .filter(ct -> ComponentLevel.GlobalLevel.equals(ct.getComponentLevel()))
        .collect(Collectors.toCollection(() -> EnumSet.noneOf(ComponentType.class)));
  }
}
