package com.yugabyte.yw.models.helpers;

import com.fasterxml.jackson.annotation.JsonAlias;
import com.fasterxml.jackson.annotation.JsonFormat;
import com.fasterxml.jackson.annotation.JsonIgnore;
import io.ebean.annotation.EnumValue;
import io.swagger.annotations.ApiModelProperty;
import java.util.Date;
import java.util.EnumSet;
import java.util.List;
import java.util.stream.Collectors;
import lombok.Data;

@Data
public class BundleDetails {

  public enum ComponentLevel {
    NodeLevel,
    GlobalLevel;
  }

  public enum ComponentType {
    @EnumValue("UniverseLogs")
    UniverseLogs(ComponentLevel.NodeLevel),

    @EnumValue("FilesComponent")
    FilesComponent(ComponentLevel.NodeLevel),

    @EnumValue("BashComponent")
    BashComponent(ComponentLevel.NodeLevel),

    @EnumValue("YSQLComponent")
    YSQLComponent(ComponentLevel.NodeLevel),

    @EnumValue("YCQLComponent")
    YCQLComponent(ComponentLevel.NodeLevel),

    @EnumValue("YbAdminComponent")
    YbAdminComponent(ComponentLevel.NodeLevel),

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

    @EnumValue("NodeAgent")
    NodeAgent(ComponentLevel.NodeLevel),

    @EnumValue("SystemLogs")
    SystemLogs(ComponentLevel.NodeLevel),

    @EnumValue("TabletReport")
    TabletReport(ComponentLevel.GlobalLevel),

    @EnumValue("K8sInfo")
    K8sInfo(ComponentLevel.GlobalLevel),

    @EnumValue("YbaMetadata")
    YbaMetadata(ComponentLevel.GlobalLevel),

    @EnumValue("PrometheusMetrics")
    PrometheusMetrics(ComponentLevel.GlobalLevel),

    @EnumValue("PerfAdvisor")
    PerfAdvisor(ComponentLevel.GlobalLevel),

    @EnumValue("ApplicationLogs")
    ApplicationLogs(ComponentLevel.GlobalLevel),

    @EnumValue("YBAComponent")
    YBAComponent(ComponentLevel.GlobalLevel);

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

  public enum PrometheusMetricsFormat {
    PROMQL_JSON,
    PROM_CHUNK
  }

  /** How to export Prometheus metrics in support bundle: PromQL query_range vs Remote Read API. */
  public enum PromExportType {
    PROMQL,
    REMOTE_READ
  }

  /**
   * Generic, spec-driven descriptor for the {@link ComponentType#FilesComponent} node-level
   * component. Each spec runs {@code scriptPath} on the node (via node-agent gRPC, SSH fallback)
   * which creates a tar at {@code remoteTarPath}; YBA then copies the tar back and untars it into
   * the per-node bundle directory. Multiple logical components (e.g. UniverseLogs) map onto the
   * single FilesComponent enum value via a list of these specs.
   */
  @Data
  public static class FilesComponentSpec implements ScriptTarComponentSpec {
    @ApiModelProperty(value = "Logical component name; used as a label/sub-directory.")
    private String componentName;

    @ApiModelProperty(
        value =
            "Path on the YBA host to the script file to execute on the node (e.g."
                + " bin/node_utils.sh). May be relative to yb.devops.home.")
    private String scriptPath;

    @ApiModelProperty(
        value =
            "Arguments appended after the script (entrypoint + flags), e.g."
                + " [create_universe_logs_bundle, --mount_path, /mnt/d0, ...].")
    private List<String> params;

    @ApiModelProperty(
        value =
            "Path on the node where the script writes the tar. May contain ${nodeName} which is"
                + " substituted per node.")
    private String remoteTarPath;

    @ApiModelProperty(value = "Linux user to run the script as on the node.")
    private String linuxUser;

    @ApiModelProperty(value = "Script execution timeout in seconds.")
    private long timeoutSecs;
  }

  /**
   * Spec-driven descriptor for {@link ComponentType#BashComponent}: runs a bash script on the node,
   * retrieves the produced tar, and untars it into the per-node bundle directory.
   */
  @Data
  public static class BashComponentSpec implements ScriptTarComponentSpec {
    @ApiModelProperty(value = "Logical component name; used as a label/sub-directory.")
    private String componentName;

    @ApiModelProperty(
        value =
            "Path on the YBA host to the script file to execute on the node (e.g."
                + " bin/node_utils.sh). May be relative to yb.devops.home.")
    private String scriptPath;

    @ApiModelProperty(
        value =
            "Arguments appended after the script (entrypoint + flags), e.g."
                + " [collect_top_stats, --remote_tar_path, /tmp/top.tar.gz, ...].")
    private List<String> params;

    @ApiModelProperty(
        value =
            "Path on the node where the script writes the tar. May contain ${nodeName} which is"
                + " substituted per node.")
    private String remoteTarPath;

    @ApiModelProperty(value = "Linux user to run the script as on the node.")
    private String linuxUser;

    @ApiModelProperty(value = "Script execution timeout in seconds.")
    private long timeoutSecs;
  }

  /** Spec for {@link ComponentType#YSQLComponent}: runs YSQL queries on the node via ysqlsh. */
  @Data
  public static class YSQLComponentSpec {
    @ApiModelProperty(value = "Logical component name; used as output file label.")
    private String componentName;

    @ApiModelProperty(value = "Database name for ysqlsh -d.")
    private String dbName;

    @ApiModelProperty(value = "YSQL statements to execute (each runs as ysqlsh -c).")
    private List<String> queries;

    @ApiModelProperty(value = "Output file name written under the per-node bundle directory.")
    private String outputFileName;

    @ApiModelProperty(value = "Command timeout in seconds.")
    private long timeoutSecs;
  }

  /** Spec for {@link ComponentType#YCQLComponent}: runs YCQL queries on the node via ycqlsh. */
  @Data
  public static class YCQLComponentSpec {
    @ApiModelProperty(value = "Logical component name; used as output file label.")
    private String componentName;

    @ApiModelProperty(value = "Optional keyspace for ycqlsh -k.")
    private String keyspace;

    @ApiModelProperty(value = "YCQL statements to execute (each runs as ycqlsh -e).")
    private List<String> queries;

    @ApiModelProperty(value = "Output file name written under the per-node bundle directory.")
    private String outputFileName;

    @ApiModelProperty(value = "Command timeout in seconds.")
    private long timeoutSecs;
  }

  /**
   * Spec-driven descriptor for {@link ComponentType#YBAComponent}: runs a bash script locally on
   * the YBA host, reads the produced tar from stdout markers, and untars it into the YBA bundle
   * directory.
   */
  @Data
  public static class YbaComponentSpec implements ScriptTarComponentSpec {
    @ApiModelProperty(value = "Logical component name; used as a label/sub-directory.")
    @JsonAlias("component_name")
    private String componentName;

    @ApiModelProperty(
        value =
            "Path on the YBA host to the script file to execute locally (e.g."
                + " bin/yba_utils.sh). May be relative to yb.devops.home.")
    @JsonAlias("script_path")
    private String scriptPath;

    @ApiModelProperty(
        value =
            "Arguments appended after the script (entrypoint + flags), e.g."
                + " [create_application_logs_bundle, --log_dir, /var/log/yba, ...].")
    private List<String> params;

    @ApiModelProperty(
        value = "Path on the YBA host where the script writes the tar before it is untarred.")
    @JsonAlias("remote_tar_path")
    private String remoteTarPath;

    @ApiModelProperty(
        value = "Ignored for local YBA execution; accepted for API parity with node-level specs.")
    @JsonAlias("linux_user")
    private String linuxUser;

    @ApiModelProperty(value = "Script execution timeout in seconds.")
    @JsonAlias("timeout_secs")
    private long timeoutSecs;
  }

  /**
   * Spec for {@link ComponentType#YbAdminComponent}: runs yb-admin on the node against the universe
   * masters.
   */
  @Data
  public static class YbAdminComponentSpec {
    @ApiModelProperty(value = "Logical component name; used as output file label.")
    private String componentName;

    @ApiModelProperty(value = "yb-admin subcommand (e.g. list_tables).")
    private String ybAdminCommand;

    @ApiModelProperty(value = "Additional arguments after the subcommand.")
    private List<String> ybAdminArgs;

    @ApiModelProperty(value = "Output file name written under the per-node bundle directory.")
    private String outputFileName;

    @ApiModelProperty(value = "Command timeout in seconds.")
    private long timeoutSecs;
  }

  public EnumSet<ComponentType> components;

  @ApiModelProperty(
      value =
          "Names of the universe nodes node-level components were collected from. Empty or null"
              + " means every node in the universe was considered.",
      required = false)
  public List<String> nodeNames;

  @ApiModelProperty(
      value = "Specs that drive the generic node-level FilesComponent (if requested).",
      required = false)
  public List<FilesComponentSpec> filesComponentSpecs;

  @ApiModelProperty(
      value = "Specs that drive the node-level BashComponent (if requested).",
      required = false)
  public List<BashComponentSpec> bashComponentSpecs;

  @ApiModelProperty(
      value = "Specs that drive the node-level YSQLComponent (if requested).",
      required = false)
  public List<YSQLComponentSpec> ysqlComponentSpecs;

  @ApiModelProperty(
      value = "Specs that drive the node-level YCQLComponent (if requested).",
      required = false)
  public List<YCQLComponentSpec> ycqlComponentSpecs;

  @ApiModelProperty(
      value = "Specs that drive the node-level YbAdminComponent (if requested).",
      required = false)
  public List<YbAdminComponentSpec> ybAdminComponentSpecs;

  @ApiModelProperty(
      value = "Specs that drive the global-level YBAComponent (if requested).",
      required = false)
  public List<YbaComponentSpec> ybaComponentSpecs;

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

  @ApiModelProperty(value = "Specifies Prom Dump metrics format.")
  public PrometheusMetricsFormat promMetricsFormat;

  @ApiModelProperty(value = "Specifies Prom Dump metrics step in seconds.")
  public Integer promMetricsStepSec;

  @ApiModelProperty(
      value = "List of exports to be included in the prometheus dump",
      required = false)
  public EnumSet<PrometheusMetricsType> prometheusMetricsTypes;

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

  @ApiModelProperty(value = "Specifies PA Dump metrics format.")
  public PrometheusMetricsFormat paMetricsFormat = PrometheusMetricsFormat.PROM_CHUNK;

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
      PrometheusMetricsFormat promMetricsFormat,
      int promMetricsStepSec,
      EnumSet<PrometheusMetricsType> prometheusMetricsTypes,
      Date paDumpStartDate,
      Date paDumpEndDate,
      PrometheusMetricsFormat paMetricsFormat) {
    this.components = components;
    this.maxNumRecentCores = maxNumRecentCores;
    this.maxCoreFileSize = maxCoreFileSize;
    this.promDumpStartDate = promDumpStartDate;
    this.promDumpEndDate = promDumpEnDate;
    this.promMetricsFormat = promMetricsFormat;
    this.promMetricsStepSec = promMetricsStepSec;
    this.prometheusMetricsTypes = prometheusMetricsTypes;
    this.paDumpStartDate = paDumpStartDate;
    this.paDumpEndDate = paDumpEndDate;
    this.paMetricsFormat = paMetricsFormat;
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
