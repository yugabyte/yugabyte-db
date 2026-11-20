/*
 * Copyright (c) YugabyteDB, Inc.
 */

package supportbundlev2

import (
	"fmt"
	"sort"
	"strconv"
	"strings"

	"github.com/sirupsen/logrus"
	ybav2client "github.com/yugabyte/platform-go-client/v2"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

func applyPrometheusMetricsParams(
	request *ybav2client.SupportBundleCreateSpec,
	cfg *supportBundleV2Config,
) error {
	componentCfg, ok := cfg.Spec.ComponentConfigs["PrometheusMetrics"]
	if !ok {
		return nil
	}
	vars := defaultTemplateVariables(cfg.Spec)
	params := componentCfg.Params
	if params == nil {
		return nil
	}

	promStart, err := collectionTimeForSpec(resolveConfigStringParam(params, "promDumpStartDate", vars))
	if err != nil {
		return fmt.Errorf("invalid promDumpStartDate: %w", err)
	}
	if promStart != nil {
		request.PromDumpStartDate = promStart
	}
	promEnd, err := collectionTimeForSpec(resolveConfigStringParam(params, "promDumpEndDate", vars))
	if err != nil {
		return fmt.Errorf("invalid promDumpEndDate: %w", err)
	}
	if promEnd != nil {
		request.PromDumpEndDate = promEnd
	}

	metricTypes := resolveConfigStringSliceParam(params, "prometheusMetricsTypes", vars)
	if len(metricTypes) == 0 {
		metricTypes = resolveConfigStringSliceParam(params, "promMetricTypes", vars)
	}
	normalizedTypes, err := normalizePrometheusMetricsTypes(metricTypes)
	if err != nil {
		return err
	}
	request.PrometheusMetricsTypes = normalizedTypes

	if promQueries := resolveConfigStringMapParam(params, "promQueries", vars); len(promQueries) > 0 {
		request.PromQueries = &promQueries
	}
	promExportType, err := promExportTypeFromManifest(
		resolveConfigStringParam(params, "promExportType", vars))
	if err != nil {
		return err
	}
	if promExportType != nil {
		request.PromExportType = promExportType
	}
	promMetricsFormat, err := promMetricsFormatFromManifest(
		resolveConfigStringParam(params, "promMetricsFormat", vars), "promMetricsFormat")
	if err != nil {
		return err
	}
	if promMetricsFormat != nil {
		request.PromMetricsFormat = promMetricsFormat
	}
	if downSample, ok := optionalConfigBoolParam(params, "promDumpDownSample"); ok {
		request.PromDumpDownSample = &downSample
	}
	return validatePrometheusMetricsParams(request)
}
func applyPerfAdvisorParams(
	request *ybav2client.SupportBundleCreateSpec,
	cfg *supportBundleV2Config,
) error {
	componentCfg, ok := cfg.Spec.ComponentConfigs["PerfAdvisor"]
	if !ok {
		return nil
	}
	vars := defaultTemplateVariables(cfg.Spec)
	params := componentCfg.Params
	if params == nil {
		return nil
	}

	paStart, err := collectionTimeForSpec(resolveConfigStringParam(params, "paDumpStartDate", vars))
	if err != nil {
		return fmt.Errorf("invalid paDumpStartDate: %w", err)
	}
	if paStart != nil {
		request.PaDumpStartDate = paStart
	}
	paEnd, err := collectionTimeForSpec(resolveConfigStringParam(params, "paDumpEndDate", vars))
	if err != nil {
		return fmt.Errorf("invalid paDumpEndDate: %w", err)
	}
	if paEnd != nil {
		request.PaDumpEndDate = paEnd
	}
	paMetricsFormat, err := promMetricsFormatFromManifest(
		resolveConfigStringParam(params, "paMetricsFormat", vars), "paMetricsFormat")
	if err != nil {
		return err
	}
	if paMetricsFormat != nil {
		request.PaMetricsFormat = paMetricsFormat
	}
	return nil
}

// applyUniverseLogsTopLevelParams and applyCoreFilesTopLevelParams carry manifest values the server
// reads off the create spec itself. The node scripts get the same values as script params, resolved
// through the same helpers so the two cannot disagree. Gated on the owning component being
// collected, so a request omitting UniverseLogs or CoreFiles does not carry their knobs.
func applyUniverseLogsTopLevelParams(
	request *ybav2client.SupportBundleCreateSpec,
	cfg *supportBundleV2Config,
	requested map[string]struct{},
) error {
	if _, selected := requested["UniverseLogs"]; !selected {
		return nil
	}
	componentCfg, ok := cfg.Spec.ComponentConfigs["UniverseLogs"]
	if !ok {
		return nil
	}
	vars := defaultTemplateVariables(cfg.Spec)
	filterPgAuditLogs, err := resolveConfigBoolParam(
		componentCfg.Params, "filterPgAuditLogs", cfg.Spec.Defaults.FilterPgAuditLogs, vars)
	if err != nil {
		return fmt.Errorf("UniverseLogs: %w", err)
	}
	request.SetFilterPgAuditLogs(filterPgAuditLogs)
	return nil
}
func applyCoreFilesTopLevelParams(
	request *ybav2client.SupportBundleCreateSpec,
	cfg *supportBundleV2Config,
	requested map[string]struct{},
) error {
	if _, selected := requested["CoreFiles"]; !selected {
		return nil
	}
	componentCfg, ok := cfg.Spec.ComponentConfigs["CoreFiles"]
	if !ok {
		return nil
	}
	vars := defaultTemplateVariables(cfg.Spec)
	maxNumRecentCores, err := resolveConfigIntParam(
		componentCfg.Params, "maxNumRecentCores", 1, vars)
	if err != nil {
		return fmt.Errorf("CoreFiles: %w", err)
	}
	maxCoreFileSize, err := resolveConfigInt64Param(
		componentCfg.Params, "maxCoreFileSize", 25000000000, vars)
	if err != nil {
		return fmt.Errorf("CoreFiles: %w", err)
	}
	request.SetMaxNumRecentCores(int32(maxNumRecentCores))
	request.SetMaxCoreFileSize(maxCoreFileSize)
	return nil
}

// normalizeEnumToken canonicalizes a manifest value so a config may write "prom-chunk" or
// "remote read" where the API expects PROM_CHUNK / REMOTE_READ. The generated constructors match
// exactly, so this has to run before them.
func normalizeEnumToken(value string) string {
	normalized := strings.ToUpper(strings.TrimSpace(value))
	normalized = strings.ReplaceAll(normalized, "-", "_")
	return strings.ReplaceAll(normalized, " ", "_")
}
func promExportTypeFromManifest(raw string) (*ybav2client.PromExportType, error) {
	token := normalizeEnumToken(raw)
	if util.IsEmptyString(token) {
		return nil, nil
	}
	value, err := ybav2client.NewPromExportTypeFromValue(token)
	if err != nil {
		return nil, fmt.Errorf("invalid promExportType %q: %w", raw, err)
	}
	return value, nil
}
func promMetricsFormatFromManifest(
	raw string,
	fieldName string,
) (*ybav2client.PrometheusMetricsFormat, error) {
	token := normalizeEnumToken(raw)
	if util.IsEmptyString(token) {
		return nil, nil
	}
	value, err := ybav2client.NewPrometheusMetricsFormatFromValue(token)
	if err != nil {
		return nil, fmt.Errorf("invalid %s %q: %w", fieldName, raw, err)
	}
	return value, nil
}

// validatePrometheusMetricsParams enforces the combinations the server rejects. Membership of the
// individual enums is handled by the generated constructors; these are the cross-field rules,
// which they know nothing about.
func validatePrometheusMetricsParams(request *ybav2client.SupportBundleCreateSpec) error {
	exportType := request.GetPromExportType()
	if exportType == "" {
		exportType = ybav2client.PROMQL
	}
	format := request.GetPromMetricsFormat()
	if format == "" {
		format = ybav2client.PROMQL_JSON
	}

	if exportType == ybav2client.REMOTE_READ && len(request.GetPromQueries()) > 0 {
		return fmt.Errorf(
			"promQueries are not supported when promExportType is REMOTE_READ")
	}
	if exportType == ybav2client.PROMQL && format == ybav2client.PROM_CHUNK {
		return fmt.Errorf(
			"promMetricsFormat PROM_CHUNK is not supported when promExportType is PROMQL; use PROMQL_JSON")
	}
	if exportType == ybav2client.PROMQL &&
		request.PromDumpDownSample != nil && !*request.PromDumpDownSample {
		return fmt.Errorf(
			"promDumpDownSample must be true when promExportType is PROMQL")
	}
	return nil
}

// normalizePrometheusMetricsTypes validates each entry against the generated enum rather than
// forwarding an unknown value for the server to reject.
func normalizePrometheusMetricsTypes(
	types []string,
) ([]ybav2client.PrometheusMetricsType, error) {
	if len(types) == 0 {
		return nil, nil
	}
	normalized := make([]ybav2client.PrometheusMetricsType, 0, len(types))
	for _, metricType := range types {
		token := normalizeEnumToken(metricType)
		if util.IsEmptyString(token) {
			continue
		}
		value, err := ybav2client.NewPrometheusMetricsTypeFromValue(token)
		if err != nil {
			return nil, fmt.Errorf("invalid prometheusMetricsTypes entry %q: %w", metricType, err)
		}
		normalized = append(normalized, *value)
	}
	if len(normalized) == 0 {
		return nil, nil
	}
	return normalized, nil
}

// buildComponentParams dispatches to the per-component param builder, producing the argv (entrypoint
// + flags) to append after the script.
func buildComponentParams(
	cfg *supportBundleV2Config,
	componentName string,
	componentCfg supportBundleV2Component,
) ([]string, error) {
	if isFileListComponent(componentCfg) {
		return buildFileListParams(cfg, componentName, componentCfg)
	}

	switch componentName {
	case "UniverseLogs":
		return buildUniverseLogsParams(cfg, componentName, componentCfg)
	case "ApplicationLogs":
		return buildApplicationLogsParams(cfg, componentName, componentCfg)
	case "YbcLogs":
		return buildYbcLogsParams(cfg, componentName, componentCfg)
	case "CoreFiles":
		return buildCoreFilesParams(cfg, componentName, componentCfg)
	case "SystemLogs":
		return buildSystemLogsParams(cfg, componentName, componentCfg)
	default:
		if isNodeBashComponent(cfg, componentName) {
			return buildBashComponentParams(cfg, componentName, componentCfg)
		}
		return buildGenericComponentParams(cfg, componentCfg), nil
	}
}

// buildBashComponentParams materializes the common bash contract plus a subset of resolved
// manifest defaults, then the member's own manifest params as the generic builder emits them.
// Runtime path overrides happen server-side in SupportBundleScriptParamResolver.
func buildBashComponentParams(
	cfg *supportBundleV2Config,
	componentName string,
	componentCfg supportBundleV2Component,
) ([]string, error) {
	params := make([]string, 0)
	params = append(params, componentCfg.Script.Entrypoint)

	vars := defaultTemplateVariables(cfg.Spec)
	remoteTarPath, err := requireRemoteTarPath(componentName, componentCfg.Params, vars)
	if err != nil {
		return nil, err
	}

	params = append(params, "--remote_tar_path", remoteTarPath)

	mountPath := resolveTemplateString(cfg.Spec.Defaults.MountPath, vars)
	nodeHomeDir := vars["defaults.nodeDataDirTemplate"]
	ybHomeDir := resolveTemplateString(cfg.Spec.Defaults.YbHomeDir, vars)
	if !util.IsEmptyString(mountPath) {
		params = append(params, "--mount_path", mountPath)
	}
	if !util.IsEmptyString(nodeHomeDir) {
		params = append(params, "--node_home_dir", nodeHomeDir)
	}
	if !util.IsEmptyString(ybHomeDir) {
		params = append(params, "--yb_home_dir", ybHomeDir)
	}
	return appendParamFlags(params, componentCfg, vars, genericRemoteTarPathKey), nil
}

// requireRemoteTarPath resolves where the component writes its tar, failing by name when the
// manifest field is absent. The path must be unique per bundle and per node, so ${bundleUuid} and
// ${nodeName} travel as literal tokens for the server to substitute. The fixed fallbacks this
// replaced ("/tmp/system-logs.tar.gz") carried none, so concurrent bundles overwrote each other.
func requireRemoteTarPath(
	componentName string,
	params map[string]interface{},
	vars map[string]string,
) (string, error) {
	remoteTarPath := resolveConfigStringParam(params, "remoteTarPathTemplate", vars)
	if util.IsEmptyString(remoteTarPath) {
		return "", fmt.Errorf("%s requires params.remoteTarPathTemplate", componentName)
	}
	return remoteTarPath, nil
}

// extractRemoteTarPath returns the value following the --remote_tar_path flag in params, if present.
func extractRemoteTarPath(params []string) string {
	for i := 0; i < len(params)-1; i++ {
		if params[i] == "--remote_tar_path" {
			return params[i+1]
		}
	}
	return ""
}
func buildUniverseLogsParams(
	cfg *supportBundleV2Config,
	componentName string,
	componentCfg supportBundleV2Component,
) ([]string, error) {
	params := make([]string, 0)
	params = append(params, componentCfg.Script.Entrypoint)

	vars := defaultTemplateVariables(cfg.Spec)

	mountPath := resolveConfigStringParam(componentCfg.Params, "mountPath", vars)
	vars["params.mountPath"] = mountPath
	nodeHomeDir := resolveConfigStringParam(componentCfg.Params, "nodeHomeDir", vars)
	vars["params.nodeHomeDir"] = nodeHomeDir
	masterLogsPath := resolveConfigStringParam(componentCfg.Params, "masterLogsPath", vars)
	tserverLogsPath := resolveConfigStringParam(componentCfg.Params, "tserverLogsPath", vars)
	ynpLogPath := resolveConfigStringParam(componentCfg.Params, "ynpLogPath", vars)
	remoteTarPath, err := requireRemoteTarPath(componentName, componentCfg.Params, vars)
	if err != nil {
		return nil, err
	}

	filterPgAuditLogs, err := resolveConfigBoolParam(
		componentCfg.Params, "filterPgAuditLogs", cfg.Spec.Defaults.FilterPgAuditLogs, vars)
	if err != nil {
		return nil, fmt.Errorf("%s: %w", componentName, err)
	}
	startDate := cfg.Spec.CollectionTimeRange.StartDate
	endDate := cfg.Spec.CollectionTimeRange.EndDate

	params = append(params,
		"--mount_path", mountPath,
		"--node_home_dir", nodeHomeDir,
		"--master_logs_path", masterLogsPath,
		"--tserver_logs_path", tserverLogsPath,
		"--start_date", startDate,
		"--end_date", endDate,
		"--ynp_log_path", ynpLogPath,
		"--remote_tar_path", remoteTarPath,
		"--filter_pgaudit_logs", strconv.FormatBool(filterPgAuditLogs),
	)

	// Server-side SupportBundleScriptParamResolver injects regex patterns at execution time.
	logrus.Debug("UniverseLogs regex patterns will be resolved from runtime config on the server")
	return params, nil
}

// buildGenericComponentParams materializes a component with no dedicated builder, which is how a
// manifest adds one without a Go change. The node scripts abort on an unrecognized argument, so it
// keeps their conventions: values template-resolved, a list becomes repeated flags,
// remoteTarPathTemplate goes under --remote_tar_path, and timeoutSecs is left to the spec.
func buildGenericComponentParams(
	cfg *supportBundleV2Config,
	componentCfg supportBundleV2Component,
) []string {
	params := []string{componentCfg.Script.Entrypoint}
	return appendParamFlags(params, componentCfg, defaultTemplateVariables(cfg.Spec), "")
}

// appendParamFlags appends each manifest param except timeoutSecs and skippedKey as a script flag.
func appendParamFlags(
	params []string,
	componentCfg supportBundleV2Component,
	vars map[string]string,
	skippedKey string,
) []string {
	keys := make([]string, 0, len(componentCfg.Params))
	for key := range componentCfg.Params {
		keys = append(keys, key)
	}
	sort.Strings(keys)

	for _, key := range keys {
		if key == genericTimeoutSecsKey || key == skippedKey {
			continue
		}
		flag := "--" + camelToSnake(key)
		if key == genericRemoteTarPathKey {
			flag = "--remote_tar_path"
		}

		values := resolveConfigStringSliceParam(componentCfg.Params, key, vars)
		if len(values) == 0 {
			// The key was set but resolved to nothing. Keep emitting the flag, as the dedicated
			// builders do for paths the server fills in at execution time.
			params = append(params, flag, "")
			continue
		}
		for _, value := range values {
			params = append(params, flag, value)
		}
	}
	return params
}
func buildSystemLogsParams(
	cfg *supportBundleV2Config,
	componentName string,
	componentCfg supportBundleV2Component,
) ([]string, error) {
	params := make([]string, 0)
	params = append(params, componentCfg.Script.Entrypoint)

	startDate := cfg.Spec.CollectionTimeRange.StartDate
	endDate := cfg.Spec.CollectionTimeRange.EndDate

	vars := defaultTemplateVariables(cfg.Spec)
	logDir := resolveConfigStringParam(componentCfg.Params, "logDir", vars)
	useSudo, err := resolveConfigBoolParam(componentCfg.Params, "useSudo", true, vars)
	if err != nil {
		return nil, fmt.Errorf("%s: %w", componentName, err)
	}

	if util.IsEmptyString(logDir) {
		logDir = "/var/log"
	}
	remoteTarPath, err := requireRemoteTarPath(componentName, componentCfg.Params, vars)
	if err != nil {
		return nil, err
	}

	logrus.Debug(
		"SystemLogs selection regex, journald toggle and epoch window are resolved by the " +
			"server from universe runtime config; they are not sent by the CLI\n",
	)

	params = append(params,
		"--log_dir", logDir,
		"--start_date", startDate,
		"--end_date", endDate,
		"--remote_tar_path", remoteTarPath,
		"--use_sudo", strconv.FormatBool(useSudo),
	)

	return params, nil
}
func buildApplicationLogsParams(
	cfg *supportBundleV2Config,
	componentName string,
	componentCfg supportBundleV2Component,
) ([]string, error) {
	params := make([]string, 0)
	params = append(params, componentCfg.Script.Entrypoint)

	startDate := cfg.Spec.CollectionTimeRange.StartDate
	endDate := cfg.Spec.CollectionTimeRange.EndDate

	vars := defaultTemplateVariables(cfg.Spec)
	logDir := resolveConfigStringParam(componentCfg.Params, "logDir", vars)
	remoteTarPath, err := requireRemoteTarPath(componentName, componentCfg.Params, vars)
	if err != nil {
		return nil, err
	}

	params = append(params,
		"--log_dir", logDir,
		"--start_date", startDate,
		"--end_date", endDate,
		"--remote_tar_path", remoteTarPath,
	)
	return params, nil
}
func buildYbcLogsParams(
	cfg *supportBundleV2Config,
	componentName string,
	componentCfg supportBundleV2Component,
) ([]string, error) {
	params := make([]string, 0)
	params = append(params, componentCfg.Script.Entrypoint)

	startDate := cfg.Spec.CollectionTimeRange.StartDate
	endDate := cfg.Spec.CollectionTimeRange.EndDate

	vars := defaultTemplateVariables(cfg.Spec)
	nodeHomeDir := resolveConfigStringParam(componentCfg.Params, "nodeHomeDir", vars)
	vars["params.nodeHomeDir"] = nodeHomeDir
	ybcLogsPath := resolveConfigStringParam(componentCfg.Params, "ybcLogsPath", vars)
	ybcServerConfPath := resolveConfigStringParam(componentCfg.Params, "ybcServerConfPath", vars)
	remoteTarPath, err := requireRemoteTarPath(componentName, componentCfg.Params, vars)
	if err != nil {
		return nil, err
	}

	params = append(params,
		"--node_home_dir", nodeHomeDir,
		"--ybc_logs_path", ybcLogsPath,
		"--ybc_server_conf_path", ybcServerConfPath,
		"--start_date", startDate,
		"--end_date", endDate,
		"--remote_tar_path", remoteTarPath,
	)
	// Server-side SupportBundleScriptParamResolver injects --regex-b64 at execution time.
	logrus.Debug("YbcLogs regex pattern will be resolved from runtime config on the server")
	return params, nil
}

// Manifest params keys that mark a component as one that tars a fixed list of paths relative to a
// base directory. A component carrying all three needs no dedicated builder.
const (
	fileListDirFlagKey  = "dirFlag"
	fileListFileFlagKey = "fileFlag"
	fileListFilesKey    = "files"

	// genericRemoteTarPathKey is spelled with a Template suffix in the manifest but reaches the
	// scripts as --remote_tar_path; genericTimeoutSecsKey is spec metadata, not a script argument.
	genericRemoteTarPathKey = "remoteTarPathTemplate"
	genericTimeoutSecsKey   = "timeoutSecs"
)

// buildFileListParams emits the argv for a file-list component. The manifest declares which flag
// carries the base directory but not its value: that varies per universe and node, so
// SupportBundleScriptParamResolver fills it in server-side.
func buildFileListParams(
	cfg *supportBundleV2Config,
	componentName string,
	componentCfg supportBundleV2Component,
) ([]string, error) {
	vars := defaultTemplateVariables(cfg.Spec)
	dirFlag := resolveConfigStringParam(componentCfg.Params, fileListDirFlagKey, vars)
	fileFlag := resolveConfigStringParam(componentCfg.Params, fileListFileFlagKey, vars)
	files := resolveConfigStringSliceParam(componentCfg.Params, fileListFilesKey, vars)
	remoteTarPath := resolveConfigStringParam(componentCfg.Params, "remoteTarPathTemplate", vars)

	if util.IsEmptyString(dirFlag) {
		return nil, fmt.Errorf("%s requires params.%s", componentName, fileListDirFlagKey)
	}
	if util.IsEmptyString(fileFlag) {
		return nil, fmt.Errorf("%s requires params.%s", componentName, fileListFileFlagKey)
	}
	if len(files) == 0 {
		return nil, fmt.Errorf("%s requires params.%s", componentName, fileListFilesKey)
	}
	if util.IsEmptyString(remoteTarPath) {
		return nil, fmt.Errorf("%s requires params.remoteTarPathTemplate", componentName)
	}

	params := make([]string, 0, 5+2*len(files))
	params = append(params,
		componentCfg.Script.Entrypoint,
		dirFlag, "",
		"--remote_tar_path", remoteTarPath,
	)
	for _, file := range files {
		params = append(params, fileFlag, file)
	}
	logrus.Debugf("%s %s will be resolved on the server", componentName, dirFlag)
	return params, nil
}
func buildCoreFilesParams(
	cfg *supportBundleV2Config,
	componentName string,
	componentCfg supportBundleV2Component,
) ([]string, error) {
	params := make([]string, 0)
	params = append(params, componentCfg.Script.Entrypoint)

	startDate := cfg.Spec.CollectionTimeRange.StartDate
	endDate := cfg.Spec.CollectionTimeRange.EndDate

	vars := defaultTemplateVariables(cfg.Spec)
	nodeHomeDir := resolveConfigStringParam(componentCfg.Params, "nodeHomeDir", vars)
	vars["params.nodeHomeDir"] = nodeHomeDir
	coresDir := resolveConfigStringParam(componentCfg.Params, "coresDir", vars)
	remoteTarPath, err := requireRemoteTarPath(componentName, componentCfg.Params, vars)
	if err != nil {
		return nil, err
	}
	maxNumRecentCores, err := resolveConfigIntParam(componentCfg.Params, "maxNumRecentCores", 1, vars)
	if err != nil {
		return nil, fmt.Errorf("%s: %w", componentName, err)
	}
	maxCoreFileSize, err := resolveConfigInt64Param(
		componentCfg.Params, "maxCoreFileSize", 25000000000, vars)
	if err != nil {
		return nil, fmt.Errorf("%s: %w", componentName, err)
	}

	params = append(params,
		"--node_home_dir", nodeHomeDir,
		"--cores_dir", coresDir,
		"--start_date", startDate,
		"--end_date", endDate,
		"--remote_tar_path", remoteTarPath,
		"--max_num_recent_cores", strconv.Itoa(maxNumRecentCores),
		"--max_core_file_size", strconv.FormatInt(maxCoreFileSize, 10),
	)
	return params, nil
}
