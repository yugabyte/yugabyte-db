/*
 * Copyright (c) YugabyteDB, Inc.
 */

package supportbundlev2

import (
	"fmt"
	"sort"
	"time"

	ybav2client "github.com/yugabyte/platform-go-client/v2"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

// scriptTarSpec is the CLI-side shape of a script+tar component. FilesComponentSpec,
// BashComponentSpec and YbaComponentSpec are three structurally identical generated types, so the
// manifest is materialized once here and converted per target type at assembly time. It carries no
// json tags: the generated models own the wire format.
type scriptTarSpec struct {
	ComponentName string
	ScriptPath    string
	Params        []string
	RemoteTarPath string
	LinuxUser     string
	TimeoutSecs   int64
}

// toFilesComponentSpecs converts the manifest's file components into the generated spec. The
// listed fields are always sent, empty or not; linux_user only when set. An empty slice returns
// nil, never [], which the server reads as "FilesComponent requested with no specs". Kept separate
// from the bash and YBA converters: identical today, each expected to diverge.
func toFilesComponentSpecs(specs []scriptTarSpec) []ybav2client.FilesComponentSpec {
	if len(specs) == 0 {
		return nil
	}
	converted := make([]ybav2client.FilesComponentSpec, 0, len(specs))
	for _, spec := range specs {
		entry := ybav2client.FilesComponentSpec{}
		entry.SetComponentName(spec.ComponentName)
		entry.SetScriptPath(spec.ScriptPath)
		entry.Params = spec.Params
		entry.SetRemoteTarPath(spec.RemoteTarPath)
		entry.SetTimeoutSecs(spec.TimeoutSecs)
		if !util.IsEmptyString(spec.LinuxUser) {
			entry.SetLinuxUser(spec.LinuxUser)
		}
		converted = append(converted, entry)
	}
	return converted
}

// toBashComponentSpecs converts the manifest's bash components into the generated spec. Fields are
// always sent, empty or not; linux_user only when set. An empty slice returns nil, never [], which
// the server reads as "BashComponent requested with no specs".
func toBashComponentSpecs(specs []scriptTarSpec) []ybav2client.BashComponentSpec {
	if len(specs) == 0 {
		return nil
	}
	converted := make([]ybav2client.BashComponentSpec, 0, len(specs))
	for _, spec := range specs {
		entry := ybav2client.BashComponentSpec{}
		entry.SetComponentName(spec.ComponentName)
		entry.SetScriptPath(spec.ScriptPath)
		entry.Params = spec.Params
		entry.SetRemoteTarPath(spec.RemoteTarPath)
		entry.SetTimeoutSecs(spec.TimeoutSecs)
		if !util.IsEmptyString(spec.LinuxUser) {
			entry.SetLinuxUser(spec.LinuxUser)
		}
		converted = append(converted, entry)
	}
	return converted
}

// toYbaComponentSpecs converts the manifest's YBA host components into the generated spec. Fields
// are always sent, empty or not; linux_user only when set. An empty slice returns nil, never [],
// which the server reads as "YBAComponent requested with no specs".
func toYbaComponentSpecs(specs []scriptTarSpec) []ybav2client.YbaComponentSpec {
	if len(specs) == 0 {
		return nil
	}
	converted := make([]ybav2client.YbaComponentSpec, 0, len(specs))
	for _, spec := range specs {
		entry := ybav2client.YbaComponentSpec{}
		entry.SetComponentName(spec.ComponentName)
		entry.SetScriptPath(spec.ScriptPath)
		entry.Params = spec.Params
		entry.SetRemoteTarPath(spec.RemoteTarPath)
		entry.SetTimeoutSecs(spec.TimeoutSecs)
		if !util.IsEmptyString(spec.LinuxUser) {
			entry.SetLinuxUser(spec.LinuxUser)
		}
		converted = append(converted, entry)
	}
	return converted
}
func buildSupportBundleCreateRequest(
	cfg *supportBundleV2Config,
	components []string,
	startDate string,
	endDate string,
	nodeNames []string,
) (ybav2client.SupportBundleCreateSpec, error) {
	// Every production path arrives here with the window already resolved from the flags. The
	// fallback covers direct callers that pass none; it does not read the manifest, so a stale
	// collectionTimeRange in a reused manifest cannot reach the request. From this point on both
	// fields hold formatted timestamps, which is what the component builders below read.
	if util.IsEmptyString(startDate) || util.IsEmptyString(endDate) {
		startDate, endDate = defaultCollectionWindowRange()
	}
	cfg.Spec.CollectionTimeRange.StartDate = startDate
	cfg.Spec.CollectionTimeRange.EndDate = endDate
	cfg.Spec.RequestBuildTimeUTC = time.Now().UTC().Format("20060102T150405Z")

	var fileSpecs []scriptTarSpec
	var bashSpecs []scriptTarSpec
	var ybaSpecs []scriptTarSpec
	var ysqlSpecs []ybav2client.YSQLComponentSpec
	var ycqlSpecs []ybav2client.YCQLComponentSpec
	var ybAdminSpecs []ybav2client.YbAdminComponentSpec
	requestedComponentTypes := make(map[ybav2client.SupportBundleComponentType]struct{})
	requestedComponents := make(map[string]struct{})

	for _, componentName := range components {
		if !isComponentEnabled(cfg, componentName) {
			continue
		}
		requestedComponents[componentName] = struct{}{}

		if isYbaScriptComponent(cfg, componentName) {
			componentCfg := cfg.Spec.ComponentConfigs[componentName]
			spec, err := buildYbaComponentSpec(cfg, componentName, componentCfg)
			if err != nil {
				return ybav2client.SupportBundleCreateSpec{}, fmt.Errorf(
					"failed to build YBA spec for %s: %w", componentName, err)
			}
			ybaSpecs = append(ybaSpecs, spec)
			requestedComponentTypes[ybav2client.YBAComponent] = struct{}{}
			continue
		}

		if apiType, ok := resolveLegacyGlobalComponent(cfg, componentName); ok {
			requestedComponentTypes[apiType] = struct{}{}
			continue
		}

		componentCfg, ok := cfg.Spec.ComponentConfigs[componentName]
		if !ok {
			return ybav2client.SupportBundleCreateSpec{}, fmt.Errorf(
				"component %s not found in componentConfigs", componentName)
		}

		componentType, err := resolveNodeComponentType(cfg, componentName)
		if err != nil {
			return ybav2client.SupportBundleCreateSpec{}, err
		}
		switch componentType {
		case "BashComponent":
			spec, err := buildScriptTarComponentSpec(cfg, componentName, componentCfg)
			if err != nil {
				return ybav2client.SupportBundleCreateSpec{}, fmt.Errorf(
					"failed to build bash spec for %s: %w", componentName, err)
			}
			bashSpecs = append(bashSpecs, spec)
			requestedComponentTypes[ybav2client.BashComponent] = struct{}{}
		case "YSQLComponent":
			spec, err := buildYSQLComponentSpec(cfg, componentName, componentCfg)
			if err != nil {
				return ybav2client.SupportBundleCreateSpec{}, fmt.Errorf(
					"failed to build YSQL spec for %s: %w", componentName, err)
			}
			ysqlSpecs = append(ysqlSpecs, spec)
			requestedComponentTypes[ybav2client.YSQLComponent] = struct{}{}
		case "YCQLComponent":
			spec, err := buildYCQLComponentSpec(cfg, componentName, componentCfg)
			if err != nil {
				return ybav2client.SupportBundleCreateSpec{}, fmt.Errorf(
					"failed to build YCQL spec for %s: %w", componentName, err)
			}
			ycqlSpecs = append(ycqlSpecs, spec)
			requestedComponentTypes[ybav2client.YCQLComponent] = struct{}{}
		case "YbAdminComponent":
			spec, err := buildYbAdminComponentSpec(cfg, componentName, componentCfg)
			if err != nil {
				return ybav2client.SupportBundleCreateSpec{}, fmt.Errorf(
					"failed to build YbAdmin spec for %s: %w", componentName, err)
			}
			ybAdminSpecs = append(ybAdminSpecs, spec)
			requestedComponentTypes[ybav2client.YbAdminComponent] = struct{}{}
		case "FilesComponent":
			spec, err := buildScriptTarComponentSpec(cfg, componentName, componentCfg)
			if err != nil {
				return ybav2client.SupportBundleCreateSpec{}, fmt.Errorf(
					"failed to build files spec for %s: %w", componentName, err)
			}
			fileSpecs = append(fileSpecs, spec)
			requestedComponentTypes[ybav2client.FilesComponent] = struct{}{}
		default:
			// Unreachable from nodeGroupToComponentType as it stands, and deliberately an error
			// rather than a fallback: this was the Files branch, so a type added to that map
			// without a case here - or a misspelled case label - was collected as a Files
			// component instead of failing.
			return ybav2client.SupportBundleCreateSpec{}, fmt.Errorf(
				"component %s resolved to unknown component type %q", componentName, componentType)
		}
	}

	if len(requestedComponentTypes) == 0 {
		return ybav2client.SupportBundleCreateSpec{}, fmt.Errorf("no enabled components to collect")
	}

	apiComponents := make([]ybav2client.SupportBundleComponentType, 0, len(requestedComponentTypes))
	for componentType := range requestedComponentTypes {
		apiComponents = append(apiComponents, componentType)
	}
	sort.Slice(apiComponents, func(i, j int) bool {
		return apiComponents[i] < apiComponents[j]
	})

	var err error
	request := ybav2client.SupportBundleCreateSpec{
		Components:            apiComponents,
		NodeNames:             nodeNames,
		FilesComponentSpecs:   toFilesComponentSpecs(fileSpecs),
		BashComponentSpecs:    toBashComponentSpecs(bashSpecs),
		YsqlComponentSpecs:    ysqlSpecs,
		YcqlComponentSpecs:    ycqlSpecs,
		YbAdminComponentSpecs: ybAdminSpecs,
		YbaComponentSpecs:     toYbaComponentSpecs(ybaSpecs),
	}
	if request.StartDate, err = collectionTimeForSpec(startDate); err != nil {
		return ybav2client.SupportBundleCreateSpec{}, fmt.Errorf("invalid start date: %w", err)
	}
	if request.EndDate, err = collectionTimeForSpec(endDate); err != nil {
		return ybav2client.SupportBundleCreateSpec{}, fmt.Errorf("invalid end date: %w", err)
	}

	if err := applyUniverseLogsTopLevelParams(&request, cfg, requestedComponents); err != nil {
		return ybav2client.SupportBundleCreateSpec{}, err
	}
	if err := applyCoreFilesTopLevelParams(&request, cfg, requestedComponents); err != nil {
		return ybav2client.SupportBundleCreateSpec{}, err
	}

	if containsComponentType(apiComponents, ybav2client.PrometheusMetrics) {
		if err := applyPrometheusMetricsParams(&request, cfg); err != nil {
			return ybav2client.SupportBundleCreateSpec{}, err
		}
	}
	if containsComponentType(apiComponents, ybav2client.PerfAdvisor) {
		if err := applyPerfAdvisorParams(&request, cfg); err != nil {
			return ybav2client.SupportBundleCreateSpec{}, err
		}
	}
	return request, nil
}

// buildScriptTarComponentSpec maps a yaml component into a script+tar spec for FilesComponent or
// BashComponent on the server.
func buildScriptTarComponentSpec(
	cfg *supportBundleV2Config,
	componentName string,
	componentCfg supportBundleV2Component,
) (scriptTarSpec, error) {
	timeoutSecs := cfg.Spec.Execution.TimeoutSecs
	if timeoutSecs <= 0 {
		timeoutSecs = 3600
	}
	vars := defaultTemplateVariables(cfg.Spec)
	ts, err := resolveConfigIntParam(componentCfg.Params, "timeoutSecs", 0, vars)
	if err != nil {
		return scriptTarSpec{}, err
	}
	if ts > 0 {
		timeoutSecs = int64(ts)
	}
	linuxUser := cfg.Spec.Execution.LinuxUser
	if util.IsEmptyString(linuxUser) {
		linuxUser = "yugabyte"
	}

	params, err := buildComponentParams(cfg, componentName, componentCfg)
	if err != nil {
		return scriptTarSpec{}, err
	}

	return scriptTarSpec{
		ComponentName: componentName,
		ScriptPath:    componentCfg.Script.ScriptFile,
		Params:        params,
		RemoteTarPath: resolveRemoteTarPathForSpec(cfg, componentCfg, params),
		LinuxUser:     linuxUser,
		TimeoutSecs:   timeoutSecs,
	}, nil
}
func buildYbaComponentSpec(
	cfg *supportBundleV2Config,
	componentName string,
	componentCfg supportBundleV2Component,
) (scriptTarSpec, error) {
	timeoutSecs := cfg.Spec.Execution.TimeoutSecs
	if timeoutSecs <= 0 {
		timeoutSecs = 3600
	}
	// Same override as buildScriptTarComponentSpec; without it the value is parsed and dropped.
	vars := defaultTemplateVariables(cfg.Spec)
	ts, err := resolveConfigIntParam(componentCfg.Params, "timeoutSecs", 0, vars)
	if err != nil {
		return scriptTarSpec{}, err
	}
	if ts > 0 {
		timeoutSecs = int64(ts)
	}

	params, err := buildComponentParams(cfg, componentName, componentCfg)
	if err != nil {
		return scriptTarSpec{}, err
	}

	return scriptTarSpec{
		ComponentName: componentName,
		ScriptPath:    componentCfg.Script.ScriptFile,
		Params:        params,
		RemoteTarPath: resolveRemoteTarPathForSpec(cfg, componentCfg, params),
		TimeoutSecs:   timeoutSecs,
	}, nil
}

// resolveRemoteTarPathForSpec returns the tar path from script params, falling back to the yaml
// remoteTarPathTemplate when generic param builders emit --remote_tar_path_template instead.
func resolveRemoteTarPathForSpec(
	cfg *supportBundleV2Config,
	componentCfg supportBundleV2Component,
	params []string,
) string {
	if remoteTarPath := extractRemoteTarPath(params); !util.IsEmptyString(remoteTarPath) {
		return remoteTarPath
	}
	vars := defaultTemplateVariables(cfg.Spec)
	return resolveConfigStringParam(componentCfg.Params, "remoteTarPathTemplate", vars)
}
func buildYSQLComponentSpec(
	cfg *supportBundleV2Config,
	componentName string,
	componentCfg supportBundleV2Component,
) (ybav2client.YSQLComponentSpec, error) {
	vars := defaultTemplateVariables(cfg.Spec)
	queries := resolveConfigStringSliceParam(componentCfg.Params, "queries", vars)
	if len(queries) == 0 {
		if query := resolveConfigStringParam(componentCfg.Params, "query", vars); !util.IsEmptyString(query) {
			queries = []string{query}
		}
	}
	if len(queries) == 0 {
		return ybav2client.YSQLComponentSpec{}, fmt.Errorf(
			"YSQL component %s requires params.queries", componentName)
	}
	dbName := resolveConfigStringParam(componentCfg.Params, "dbName", vars)
	outputFileName := resolveConfigStringParam(componentCfg.Params, "outputFileName", vars)
	timeoutSecs, err := resolveConfigIntParam(componentCfg.Params, "timeoutSecs", 300, vars)
	if err != nil {
		return ybav2client.YSQLComponentSpec{}, err
	}
	if util.IsEmptyString(outputFileName) {
		outputFileName = componentName + ".txt"
	}
	spec := ybav2client.YSQLComponentSpec{}
	spec.SetComponentName(componentName)
	spec.Queries = queries
	if !util.IsEmptyString(dbName) {
		spec.SetDbName(dbName)
	}
	if !util.IsEmptyString(outputFileName) {
		spec.SetOutputFileName(outputFileName)
	}
	if timeoutSecs != 0 {
		spec.SetTimeoutSecs(int64(timeoutSecs))
	}
	return spec, nil
}
func buildYCQLComponentSpec(
	cfg *supportBundleV2Config,
	componentName string,
	componentCfg supportBundleV2Component,
) (ybav2client.YCQLComponentSpec, error) {
	vars := defaultTemplateVariables(cfg.Spec)
	queries := resolveConfigStringSliceParam(componentCfg.Params, "queries", vars)
	if len(queries) == 0 {
		if query := resolveConfigStringParam(componentCfg.Params, "query", vars); !util.IsEmptyString(query) {
			queries = []string{query}
		}
	}
	if len(queries) == 0 {
		return ybav2client.YCQLComponentSpec{}, fmt.Errorf(
			"YCQL component %s requires params.queries", componentName)
	}
	keyspace := resolveConfigStringParam(componentCfg.Params, "keyspace", vars)
	outputFileName := resolveConfigStringParam(componentCfg.Params, "outputFileName", vars)
	timeoutSecs, err := resolveConfigIntParam(componentCfg.Params, "timeoutSecs", 300, vars)
	if err != nil {
		return ybav2client.YCQLComponentSpec{}, err
	}
	if util.IsEmptyString(outputFileName) {
		outputFileName = componentName + ".txt"
	}
	spec := ybav2client.YCQLComponentSpec{}
	spec.SetComponentName(componentName)
	spec.Queries = queries
	if !util.IsEmptyString(keyspace) {
		spec.SetKeyspace(keyspace)
	}
	if !util.IsEmptyString(outputFileName) {
		spec.SetOutputFileName(outputFileName)
	}
	if timeoutSecs != 0 {
		spec.SetTimeoutSecs(int64(timeoutSecs))
	}
	return spec, nil
}
func buildYbAdminComponentSpec(
	cfg *supportBundleV2Config,
	componentName string,
	componentCfg supportBundleV2Component,
) (ybav2client.YbAdminComponentSpec, error) {
	vars := defaultTemplateVariables(cfg.Spec)
	commands := resolveConfigStringSliceParam(componentCfg.Params, "ybAdminCommands", vars)
	if len(commands) == 0 {
		return ybav2client.YbAdminComponentSpec{}, fmt.Errorf(
			"YbAdmin component %s requires params.ybAdminCommands", componentName)
	}
	ybAdminArgs := resolveConfigStringSliceParam(componentCfg.Params, "ybAdminArgs", vars)
	outputFileName := resolveConfigStringParam(componentCfg.Params, "outputFileName", vars)
	timeoutSecs, err := resolveConfigIntParam(componentCfg.Params, "timeoutSecs", 300, vars)
	if err != nil {
		return ybav2client.YbAdminComponentSpec{}, err
	}
	if util.IsEmptyString(outputFileName) {
		outputFileName = componentName + ".txt"
	}
	spec := ybav2client.YbAdminComponentSpec{}
	spec.SetComponentName(componentName)
	spec.YbAdminCommands = commands
	if len(ybAdminArgs) > 0 {
		spec.YbAdminArgs = ybAdminArgs
	}
	if !util.IsEmptyString(outputFileName) {
		spec.SetOutputFileName(outputFileName)
	}
	if timeoutSecs != 0 {
		spec.SetTimeoutSecs(int64(timeoutSecs))
	}
	return spec, nil
}
