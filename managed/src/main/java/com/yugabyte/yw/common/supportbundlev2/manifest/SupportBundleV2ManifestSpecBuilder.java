// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.supportbundlev2.manifest;

import static play.mvc.Http.Status.INTERNAL_SERVER_ERROR;

import api.v2.models.BashComponentSpec;
import api.v2.models.FilesComponentSpec;
import api.v2.models.YCQLComponentSpec;
import api.v2.models.YSQLComponentSpec;
import api.v2.models.YbAdminComponentSpec;
import api.v2.models.YbaComponentSpec;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.supportbundlev2.manifest.SupportBundleV2Manifest.Component;
import com.yugabyte.yw.common.supportbundlev2.manifest.SupportBundleV2Manifest.Spec;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;
import lombok.Value;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

/**
 * Turns one {@code componentConfigs} entry into the spec object the v2 create API expects.
 *
 * <p>Port of the {@code build*ComponentSpec} / {@code build*Params} functions in {@code
 * yba-cli/cmd/universe/supportbundlev2/supportbundlev2.go}. The argv this produces has to match
 * what the CLI produces for the same manifest, because both feed the same node scripts.
 *
 * <p>Several flags are emitted with an empty value on purpose ({@code --mount_path}, {@code
 * --node_home_dir}, the file-list dir flag, and the log path flags). Their values depend on the
 * node's provider, data directory and gflag overrides, so {@code SupportBundleScriptParamResolver}
 * fills them in at execution time. Dropping the flag instead of emitting it empty would break the
 * script's argument contract.
 */
@Slf4j
public final class SupportBundleV2ManifestSpecBuilder {

  /**
   * A component carrying all three of these tars a fixed list of paths under one base directory, so
   * it needs no dedicated builder.
   */
  private static final String FILE_LIST_DIR_FLAG_KEY = "dirFlag";

  private static final String FILE_LIST_FILE_FLAG_KEY = "fileFlag";
  private static final String FILE_LIST_FILES_KEY = "files";

  private static final String REMOTE_TAR_PATH_TEMPLATE_KEY = "remoteTarPathTemplate";
  private static final String REMOTE_TAR_PATH_FLAG = "--remote_tar_path";
  private static final String TIMEOUT_SECS_KEY = "timeoutSecs";

  private static final String BASH_GROUP = "Bash";

  // Also what components use when a spec carries no timeout.
  public static final long DEFAULT_TIMEOUT_SECS = 3600L;
  public static final long DEFAULT_QUERY_TIMEOUT_SECS = 300L;
  private static final String DEFAULT_LINUX_USER = "yugabyte";

  private SupportBundleV2ManifestSpecBuilder() {}

  public static FilesComponentSpec buildFilesComponentSpec(
      Spec spec, String componentName, Component componentCfg) {
    ScriptTarSpec scriptTar = buildScriptTarSpec(spec, componentName, componentCfg, true);
    return new FilesComponentSpec()
        .componentName(scriptTar.getComponentName())
        .scriptPath(scriptTar.getScriptPath())
        .params(scriptTar.getParams())
        .remoteTarPath(scriptTar.getRemoteTarPath())
        .linuxUser(scriptTar.getLinuxUser())
        .timeoutSecs(scriptTar.getTimeoutSecs());
  }

  public static BashComponentSpec buildBashComponentSpec(
      Spec spec, String componentName, Component componentCfg) {
    ScriptTarSpec scriptTar = buildScriptTarSpec(spec, componentName, componentCfg, true);
    return new BashComponentSpec()
        .componentName(scriptTar.getComponentName())
        .scriptPath(scriptTar.getScriptPath())
        .params(scriptTar.getParams())
        .remoteTarPath(scriptTar.getRemoteTarPath())
        .linuxUser(scriptTar.getLinuxUser())
        .timeoutSecs(scriptTar.getTimeoutSecs());
  }

  /** YBA components run on the platform host as the YBA service user, so no linux user is set. */
  public static YbaComponentSpec buildYbaComponentSpec(
      Spec spec, String componentName, Component componentCfg) {
    ScriptTarSpec scriptTar = buildScriptTarSpec(spec, componentName, componentCfg, false);
    return new YbaComponentSpec()
        .componentName(scriptTar.getComponentName())
        .scriptPath(scriptTar.getScriptPath())
        .params(scriptTar.getParams())
        .remoteTarPath(scriptTar.getRemoteTarPath())
        .timeoutSecs(scriptTar.getTimeoutSecs());
  }

  public static YSQLComponentSpec buildYsqlComponentSpec(
      Spec spec, String componentName, Component componentCfg) {
    Map<String, String> vars =
        SupportBundleV2ManifestTemplateResolver.defaultTemplateVariables(spec);
    return new YSQLComponentSpec()
        .componentName(componentName)
        .dbName(
            blankToNull(
                SupportBundleV2ManifestTemplateResolver.resolveConfigString(
                    componentCfg.getParams(), "dbName", vars)))
        .queries(requireQueries(componentName, "YSQL", componentCfg, vars))
        .outputFileName(resolveOutputFileName(componentName, componentCfg, vars))
        .timeoutSecs(queryTimeoutSecs(spec, componentCfg));
  }

  public static YCQLComponentSpec buildYcqlComponentSpec(
      Spec spec, String componentName, Component componentCfg) {
    Map<String, String> vars =
        SupportBundleV2ManifestTemplateResolver.defaultTemplateVariables(spec);
    return new YCQLComponentSpec()
        .componentName(componentName)
        .keyspace(
            blankToNull(
                SupportBundleV2ManifestTemplateResolver.resolveConfigString(
                    componentCfg.getParams(), "keyspace", vars)))
        .queries(requireQueries(componentName, "YCQL", componentCfg, vars))
        .outputFileName(resolveOutputFileName(componentName, componentCfg, vars))
        .timeoutSecs(queryTimeoutSecs(spec, componentCfg));
  }

  public static YbAdminComponentSpec buildYbAdminComponentSpec(
      Spec spec, String componentName, Component componentCfg) {
    Map<String, String> vars =
        SupportBundleV2ManifestTemplateResolver.defaultTemplateVariables(spec);
    List<String> commands =
        SupportBundleV2ManifestTemplateResolver.resolveConfigStringList(
            componentCfg.getParams(), "ybAdminCommands", vars);
    if (commands.isEmpty()) {
      throw manifestError("YbAdmin component %s requires params.ybAdminCommands", componentName);
    }
    return new YbAdminComponentSpec()
        .componentName(componentName)
        .ybAdminCommands(commands)
        .ybAdminArgs(
            emptyToNull(
                SupportBundleV2ManifestTemplateResolver.resolveConfigStringList(
                    componentCfg.getParams(), "ybAdminArgs", vars)))
        .outputFileName(resolveOutputFileName(componentName, componentCfg, vars))
        .timeoutSecs(queryTimeoutSecs(spec, componentCfg));
  }

  /** A Files, Bash or YBA component's timeout: its own timeoutSecs, else the execution default. */
  static long scriptTimeoutSecs(Spec spec, Component componentCfg) {
    long componentTimeoutSecs =
        SupportBundleV2ManifestTemplateResolver.resolveConfigLong(
            componentCfg.getParams(),
            TIMEOUT_SECS_KEY,
            0,
            SupportBundleV2ManifestTemplateResolver.defaultTemplateVariables(spec));
    if (componentTimeoutSecs > 0) {
      return componentTimeoutSecs;
    }
    long timeoutSecs = spec.getExecution().getTimeoutSecs();
    return timeoutSecs > 0 ? timeoutSecs : DEFAULT_TIMEOUT_SECS;
  }

  /** A YSQL, YCQL or yb-admin member's timeout. */
  static long queryTimeoutSecs(Spec spec, Component componentCfg) {
    return SupportBundleV2ManifestTemplateResolver.resolveConfigLong(
        componentCfg.getParams(),
        TIMEOUT_SECS_KEY,
        DEFAULT_QUERY_TIMEOUT_SECS,
        SupportBundleV2ManifestTemplateResolver.defaultTemplateVariables(spec));
  }

  private static ScriptTarSpec buildScriptTarSpec(
      Spec spec, String componentName, Component componentCfg, boolean pinLinuxUser) {
    Map<String, String> vars =
        SupportBundleV2ManifestTemplateResolver.defaultTemplateVariables(spec);

    long timeoutSecs = scriptTimeoutSecs(spec, componentCfg);

    String linuxUser = null;
    if (pinLinuxUser) {
      linuxUser = spec.getExecution().getLinuxUser();
      if (StringUtils.isBlank(linuxUser)) {
        linuxUser = DEFAULT_LINUX_USER;
      }
    }

    List<String> params = buildComponentParams(spec, componentName, componentCfg);
    return new ScriptTarSpec(
        componentName,
        componentCfg.getScript().getScriptFile(),
        params,
        resolveRemoteTarPathForSpec(vars, componentCfg, params),
        linuxUser,
        timeoutSecs);
  }

  /**
   * Prefers the tar path already present in the argv, falling back to the manifest template for
   * components whose argv is produced by the generic builder and therefore names the flag {@code
   * --remote_tar_path_template}.
   */
  private static String resolveRemoteTarPathForSpec(
      Map<String, String> vars, Component componentCfg, List<String> params) {
    String fromParams = extractRemoteTarPath(params);
    if (StringUtils.isNotBlank(fromParams)) {
      return fromParams;
    }
    return SupportBundleV2ManifestTemplateResolver.resolveConfigString(
        componentCfg.getParams(), REMOTE_TAR_PATH_TEMPLATE_KEY, vars);
  }

  static String extractRemoteTarPath(List<String> params) {
    for (int i = 0; i < params.size() - 1; i++) {
      if (REMOTE_TAR_PATH_FLAG.equals(params.get(i))) {
        return params.get(i + 1);
      }
    }
    return "";
  }

  /** Produces the argv appended after the script: the entrypoint followed by its flags. */
  static List<String> buildComponentParams(
      Spec spec, String componentName, Component componentCfg) {
    if (isFileListComponent(componentCfg)) {
      return buildFileListParams(spec, componentName, componentCfg);
    }
    switch (componentName) {
      case "UniverseLogs":
        return buildUniverseLogsParams(spec, componentCfg);
      case "ApplicationLogs":
        return buildApplicationLogsParams(spec, componentCfg);
      case "YbcLogs":
        return buildYbcLogsParams(spec, componentCfg);
      case "CoreFiles":
        return buildCoreFilesParams(spec, componentCfg);
      case "SystemLogs":
        return buildSystemLogsParams(spec, componentCfg);
      default:
        if (isNodeBashComponent(spec, componentName)) {
          return buildBashComponentParams(spec, componentCfg);
        }
        return buildGenericComponentParams(spec, componentCfg);
    }
  }

  static boolean isFileListComponent(Component componentCfg) {
    Map<String, Object> params = componentCfg.getParams();
    return params != null
        && params.containsKey(FILE_LIST_DIR_FLAG_KEY)
        && params.containsKey(FILE_LIST_FILE_FLAG_KEY)
        && params.containsKey(FILE_LIST_FILES_KEY);
  }

  private static boolean isNodeBashComponent(Spec spec, String componentName) {
    List<String> bashComponents = spec.getNodeComponents().get(BASH_GROUP);
    return bashComponents != null && containsTrimmed(bashComponents, componentName);
  }

  private static List<String> buildFileListParams(
      Spec spec, String componentName, Component componentCfg) {
    Map<String, String> vars =
        SupportBundleV2ManifestTemplateResolver.defaultTemplateVariables(spec);
    Map<String, Object> cfgParams = componentCfg.getParams();

    String dirFlag =
        SupportBundleV2ManifestTemplateResolver.resolveConfigString(
            cfgParams, FILE_LIST_DIR_FLAG_KEY, vars);
    String fileFlag =
        SupportBundleV2ManifestTemplateResolver.resolveConfigString(
            cfgParams, FILE_LIST_FILE_FLAG_KEY, vars);
    List<String> files =
        SupportBundleV2ManifestTemplateResolver.resolveConfigStringList(
            cfgParams, FILE_LIST_FILES_KEY, vars);
    String remoteTarPath =
        SupportBundleV2ManifestTemplateResolver.resolveConfigString(
            cfgParams, REMOTE_TAR_PATH_TEMPLATE_KEY, vars);

    requireNotBlank(componentName, FILE_LIST_DIR_FLAG_KEY, dirFlag);
    requireNotBlank(componentName, FILE_LIST_FILE_FLAG_KEY, fileFlag);
    requireNotBlank(componentName, REMOTE_TAR_PATH_TEMPLATE_KEY, remoteTarPath);
    if (files.isEmpty()) {
      throw manifestError("%s requires params.%s", componentName, FILE_LIST_FILES_KEY);
    }

    List<String> params = new ArrayList<>(5 + 2 * files.size());
    params.addAll(
        Arrays.asList(
            componentCfg.getScript().getEntrypoint(),
            dirFlag,
            "",
            REMOTE_TAR_PATH_FLAG,
            remoteTarPath));
    for (String file : files) {
      params.add(fileFlag);
      params.add(file);
    }
    return params;
  }

  private static List<String> buildUniverseLogsParams(Spec spec, Component componentCfg) {
    Map<String, String> vars =
        SupportBundleV2ManifestTemplateResolver.defaultTemplateVariables(spec);
    Map<String, Object> cfgParams = componentCfg.getParams();

    String mountPath =
        SupportBundleV2ManifestTemplateResolver.resolveConfigString(cfgParams, "mountPath", vars);
    vars.put("params.mountPath", mountPath);
    String nodeHomeDir =
        SupportBundleV2ManifestTemplateResolver.resolveConfigString(cfgParams, "nodeHomeDir", vars);
    vars.put("params.nodeHomeDir", nodeHomeDir);

    return new ArrayList<>(
        Arrays.asList(
            componentCfg.getScript().getEntrypoint(),
            "--mount_path",
            mountPath,
            "--node_home_dir",
            nodeHomeDir,
            "--master_logs_path",
            SupportBundleV2ManifestTemplateResolver.resolveConfigString(
                cfgParams, "masterLogsPath", vars),
            "--tserver_logs_path",
            SupportBundleV2ManifestTemplateResolver.resolveConfigString(
                cfgParams, "tserverLogsPath", vars),
            "--start_date",
            startDate(spec),
            "--end_date",
            endDate(spec),
            "--ynp_log_path",
            SupportBundleV2ManifestTemplateResolver.resolveConfigString(
                cfgParams, "ynpLogPath", vars),
            REMOTE_TAR_PATH_FLAG,
            SupportBundleV2ManifestTemplateResolver.resolveConfigString(
                cfgParams, REMOTE_TAR_PATH_TEMPLATE_KEY, vars),
            "--filter_pgaudit_logs",
            String.valueOf(
                SupportBundleV2ManifestTemplateResolver.resolveConfigBool(
                    cfgParams,
                    "filterPgAuditLogs",
                    spec.getDefaults().isFilterPgAuditLogs(),
                    vars))));
  }

  private static List<String> buildApplicationLogsParams(Spec spec, Component componentCfg) {
    Map<String, String> vars =
        SupportBundleV2ManifestTemplateResolver.defaultTemplateVariables(spec);
    Map<String, Object> cfgParams = componentCfg.getParams();

    String remoteTarPath =
        SupportBundleV2ManifestTemplateResolver.resolveConfigString(
            cfgParams, REMOTE_TAR_PATH_TEMPLATE_KEY, vars);
    if (StringUtils.isBlank(remoteTarPath)) {
      remoteTarPath = "/tmp/application-logs.tar.gz";
    }
    return new ArrayList<>(
        Arrays.asList(
            componentCfg.getScript().getEntrypoint(),
            "--log_dir",
            SupportBundleV2ManifestTemplateResolver.resolveConfigString(cfgParams, "logDir", vars),
            "--start_date",
            startDate(spec),
            "--end_date",
            endDate(spec),
            REMOTE_TAR_PATH_FLAG,
            remoteTarPath));
  }

  private static List<String> buildYbcLogsParams(Spec spec, Component componentCfg) {
    Map<String, String> vars =
        SupportBundleV2ManifestTemplateResolver.defaultTemplateVariables(spec);
    Map<String, Object> cfgParams = componentCfg.getParams();

    String nodeHomeDir =
        SupportBundleV2ManifestTemplateResolver.resolveConfigString(cfgParams, "nodeHomeDir", vars);
    vars.put("params.nodeHomeDir", nodeHomeDir);
    String remoteTarPath =
        SupportBundleV2ManifestTemplateResolver.resolveConfigString(
            cfgParams, REMOTE_TAR_PATH_TEMPLATE_KEY, vars);
    if (StringUtils.isBlank(remoteTarPath)) {
      remoteTarPath = "/tmp/ybc-logs.tar.gz";
    }
    return new ArrayList<>(
        Arrays.asList(
            componentCfg.getScript().getEntrypoint(),
            "--node_home_dir",
            nodeHomeDir,
            "--ybc_logs_path",
            SupportBundleV2ManifestTemplateResolver.resolveConfigString(
                cfgParams, "ybcLogsPath", vars),
            "--ybc_server_conf_path",
            SupportBundleV2ManifestTemplateResolver.resolveConfigString(
                cfgParams, "ybcServerConfPath", vars),
            "--start_date",
            startDate(spec),
            "--end_date",
            endDate(spec),
            REMOTE_TAR_PATH_FLAG,
            remoteTarPath));
  }

  private static List<String> buildCoreFilesParams(Spec spec, Component componentCfg) {
    Map<String, String> vars =
        SupportBundleV2ManifestTemplateResolver.defaultTemplateVariables(spec);
    Map<String, Object> cfgParams = componentCfg.getParams();

    String nodeHomeDir =
        SupportBundleV2ManifestTemplateResolver.resolveConfigString(cfgParams, "nodeHomeDir", vars);
    vars.put("params.nodeHomeDir", nodeHomeDir);
    String remoteTarPath =
        SupportBundleV2ManifestTemplateResolver.resolveConfigString(
            cfgParams, REMOTE_TAR_PATH_TEMPLATE_KEY, vars);
    if (StringUtils.isBlank(remoteTarPath)) {
      remoteTarPath = "/tmp/core-files.tar.gz";
    }
    return new ArrayList<>(
        Arrays.asList(
            componentCfg.getScript().getEntrypoint(),
            "--node_home_dir",
            nodeHomeDir,
            "--cores_dir",
            SupportBundleV2ManifestTemplateResolver.resolveConfigString(
                cfgParams, "coresDir", vars),
            "--start_date",
            startDate(spec),
            "--end_date",
            endDate(spec),
            REMOTE_TAR_PATH_FLAG,
            remoteTarPath,
            "--max_num_recent_cores",
            String.valueOf(
                SupportBundleV2ManifestTemplateResolver.resolveConfigInt(
                    cfgParams, "maxNumRecentCores", 1, vars)),
            "--max_core_file_size",
            String.valueOf(
                SupportBundleV2ManifestTemplateResolver.resolveConfigLong(
                    cfgParams, "maxCoreFileSize", 25000000000L, vars))));
  }

  private static List<String> buildSystemLogsParams(Spec spec, Component componentCfg) {
    Map<String, String> vars =
        SupportBundleV2ManifestTemplateResolver.defaultTemplateVariables(spec);
    Map<String, Object> cfgParams = componentCfg.getParams();

    String logDir =
        SupportBundleV2ManifestTemplateResolver.resolveConfigString(cfgParams, "logDir", vars);
    String remoteTarPath =
        SupportBundleV2ManifestTemplateResolver.resolveConfigString(
            cfgParams, REMOTE_TAR_PATH_TEMPLATE_KEY, vars);
    if (StringUtils.isBlank(logDir)) {
      logDir = "/var/log";
    }
    if (StringUtils.isBlank(remoteTarPath)) {
      remoteTarPath = "/tmp/system-logs.tar.gz";
    }
    // The selection regex, the journald toggle and the epoch window are appended by
    // SupportBundleScriptParamResolver from universe-scoped runtime config, the same way the
    // universe log regexes are, so they are deliberately absent here.
    return new ArrayList<>(
        Arrays.asList(
            componentCfg.getScript().getEntrypoint(),
            "--log_dir",
            logDir,
            "--start_date",
            startDate(spec),
            "--end_date",
            endDate(spec),
            REMOTE_TAR_PATH_FLAG,
            remoteTarPath,
            "--use_sudo",
            String.valueOf(
                SupportBundleV2ManifestTemplateResolver.resolveConfigBool(
                    cfgParams, "useSudo", true, vars))));
  }

  /**
   * The bash contract every {@code collect_*} entrypoint shares, then the member's own manifest
   * params as flags, as the generic builder emits them. The defaults are overridden per node at
   * execution time.
   */
  private static List<String> buildBashComponentParams(Spec spec, Component componentCfg) {
    Map<String, String> vars =
        SupportBundleV2ManifestTemplateResolver.defaultTemplateVariables(spec);
    String remoteTarPath =
        SupportBundleV2ManifestTemplateResolver.resolveConfigString(
            componentCfg.getParams(), REMOTE_TAR_PATH_TEMPLATE_KEY, vars);
    if (StringUtils.isBlank(remoteTarPath)) {
      throw manifestError("Bash component requires params.%s", REMOTE_TAR_PATH_TEMPLATE_KEY);
    }

    List<String> params = new ArrayList<>();
    params.add(componentCfg.getScript().getEntrypoint());
    params.add(REMOTE_TAR_PATH_FLAG);
    params.add(remoteTarPath);

    addFlagIfPresent(
        params,
        "--mount_path",
        SupportBundleV2ManifestTemplateResolver.resolveTemplateString(
            spec.getDefaults().getMountPath(), vars));
    addFlagIfPresent(params, "--node_home_dir", vars.get("defaults.nodeDataDirTemplate"));
    addFlagIfPresent(
        params,
        "--yb_home_dir",
        SupportBundleV2ManifestTemplateResolver.resolveTemplateString(
            spec.getDefaults().getYbHomeDir(), vars));
    appendParamFlags(params, componentCfg, vars, REMOTE_TAR_PATH_TEMPLATE_KEY);
    return params;
  }

  /**
   * Fallback for a component with no dedicated builder, which is how a manifest adds one without a
   * Java change. The node scripts abort on an unknown argument, so it keeps their conventions, as
   * the CLI's buildGenericComponentParams does: values resolved, a list becomes repeated flags,
   * remoteTarPathTemplate goes under --remote_tar_path, and timeoutSecs is left to the spec.
   */
  private static List<String> buildGenericComponentParams(Spec spec, Component componentCfg) {
    List<String> params = new ArrayList<>();
    params.add(componentCfg.getScript().getEntrypoint());
    appendParamFlags(
        params,
        componentCfg,
        SupportBundleV2ManifestTemplateResolver.defaultTemplateVariables(spec),
        null);
    return params;
  }

  /** Appends each manifest param except timeoutSecs and {@code skippedKey} as a script flag. */
  private static void appendParamFlags(
      List<String> params, Component componentCfg, Map<String, String> vars, String skippedKey) {
    if (componentCfg.getParams() == null) {
      return;
    }
    for (String key : new TreeMap<>(componentCfg.getParams()).keySet()) {
      if (TIMEOUT_SECS_KEY.equals(key) || key.equals(skippedKey)) {
        continue;
      }
      String flag =
          REMOTE_TAR_PATH_TEMPLATE_KEY.equals(key)
              ? REMOTE_TAR_PATH_FLAG
              : "--" + SupportBundleV2ManifestTemplateResolver.camelToSnake(key);
      List<String> values =
          SupportBundleV2ManifestTemplateResolver.resolveConfigStringList(
              componentCfg.getParams(), key, vars);
      if (values.isEmpty()) {
        // Set but empty: keep the flag, as the dedicated builders do for values filled in later.
        params.add(flag);
        params.add("");
        continue;
      }
      for (String value : values) {
        params.add(flag);
        params.add(value);
      }
    }
  }

  private static List<String> requireQueries(
      String componentName, String language, Component componentCfg, Map<String, String> vars) {
    List<String> queries =
        SupportBundleV2ManifestTemplateResolver.resolveConfigStringList(
            componentCfg.getParams(), "queries", vars);
    if (queries.isEmpty()) {
      String query =
          SupportBundleV2ManifestTemplateResolver.resolveConfigString(
              componentCfg.getParams(), "query", vars);
      if (StringUtils.isNotBlank(query)) {
        queries.add(query);
      }
    }
    if (queries.isEmpty()) {
      throw manifestError("%s component %s requires params.queries", language, componentName);
    }
    return queries;
  }

  private static String resolveOutputFileName(
      String componentName, Component componentCfg, Map<String, String> vars) {
    String outputFileName =
        SupportBundleV2ManifestTemplateResolver.resolveConfigString(
            componentCfg.getParams(), "outputFileName", vars);
    return StringUtils.isBlank(outputFileName) ? componentName + ".txt" : outputFileName;
  }

  /**
   * Optional spec fields are left unset rather than blank, so the payload built here is identical
   * to the one the CLI sends, which drops them through {@code omitempty}.
   */
  private static String blankToNull(String value) {
    return StringUtils.isBlank(value) ? null : value;
  }

  private static List<String> emptyToNull(List<String> values) {
    return values.isEmpty() ? null : values;
  }

  private static void addFlagIfPresent(List<String> params, String flag, String value) {
    if (StringUtils.isNotBlank(value)) {
      params.add(flag);
      params.add(value);
    }
  }

  private static void requireNotBlank(String componentName, String key, String value) {
    if (StringUtils.isBlank(value)) {
      throw manifestError("%s requires params.%s", componentName, key);
    }
  }

  private static String startDate(Spec spec) {
    return StringUtils.trimToEmpty(spec.getCollectionTimeRange().getStartDate());
  }

  private static String endDate(Spec spec) {
    return StringUtils.trimToEmpty(spec.getCollectionTimeRange().getEndDate());
  }

  /**
   * The manifest is operator owned server side configuration, not caller input, so a defect in it
   * is a 500 rather than a 400.
   */
  private static PlatformServiceException manifestError(String format, Object... args) {
    return new PlatformServiceException(
        INTERNAL_SERVER_ERROR, String.format("Invalid support bundle manifest: " + format, args));
  }

  private static boolean containsTrimmed(List<String> values, String target) {
    for (String value : values) {
      if (value != null && value.trim().equals(target)) {
        return true;
      }
    }
    return false;
  }

  @Value
  private static class ScriptTarSpec {
    String componentName;
    String scriptPath;
    List<String> params;
    String remoteTarPath;
    String linuxUser;
    long timeoutSecs;
  }
}
