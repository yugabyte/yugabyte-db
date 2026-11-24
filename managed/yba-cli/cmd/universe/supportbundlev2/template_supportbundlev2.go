/*
 * Copyright (c) YugabyteDB, Inc.
 */

package supportbundlev2

import (
	"fmt"
	"strconv"
	"strings"
	"time"

	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

func optionalConfigBoolParam(params map[string]interface{}, key string) (bool, bool) {
	raw, ok := params[key]
	if !ok || raw == nil {
		return false, false
	}
	switch typed := raw.(type) {
	case bool:
		return typed, true
	case string:
		value := strings.TrimSpace(strings.ToLower(typed))
		if value == "true" {
			return true, true
		}
		if value == "false" {
			return false, true
		}
	}
	return false, false
}
func resolveConfigStringMapParam(
	params map[string]interface{},
	key string,
	vars map[string]string,
) map[string]string {
	raw, ok := params[key]
	if !ok || raw == nil {
		return nil
	}
	result := make(map[string]string)
	switch typed := raw.(type) {
	case map[string]string:
		for mapKey, mapValue := range typed {
			result[mapKey] = resolveTemplateString(mapValue, vars)
		}
	case map[string]interface{}:
		for mapKey, mapValue := range typed {
			result[mapKey] = resolveTemplateString(fmt.Sprintf("%v", mapValue), vars)
		}
	default:
		return nil
	}
	return result
}
func defaultTemplateVariables(spec supportBundleV2Spec) map[string]string {
	vars := map[string]string{
		"defaults.mountPath":            spec.Defaults.MountPath,
		"defaults.nodeDataDirTemplate":  spec.Defaults.NodeDataDirTemplate,
		"defaults.ybHomeDir":            spec.Defaults.YbHomeDir,
		"defaults.remoteTmpDir":         spec.Defaults.RemoteTmpDir,
		"defaults.nodeAgentInstallPath": spec.Defaults.NodeAgentInstallPath,
		"collectionTimeRange.startDate": spec.CollectionTimeRange.StartDate,
		"collectionTimeRange.endDate":   spec.CollectionTimeRange.EndDate,
		// Shorthand used by defaults.nodeDataDirTemplate ("${mountPath}/yb-data").
		"mountPath": spec.Defaults.MountPath,
	}
	vars["defaults.nodeDataDirTemplate"] = resolveTemplateString(
		spec.Defaults.NodeDataDirTemplate, vars)
	// buildSupportBundleCreateRequest pins RequestBuildTimeUTC before any builder runs, so every
	// call here returns the same stamp. The fallback only covers a caller that skipped that step;
	// it must not become the normal path, or each of this function's callers would stamp a
	// different time into the same request.
	timestampUTC := spec.RequestBuildTimeUTC
	if util.IsEmptyString(timestampUTC) {
		timestampUTC = time.Now().UTC().Format("20060102T150405Z")
	}
	vars["currentTimestampUTC"] = timestampUTC
	return vars
}
func resolveConfigStringParam(
	params map[string]interface{},
	key string,
	vars map[string]string,
) string {
	raw, ok := params[key]
	if !ok || raw == nil {
		return ""
	}
	value := fmt.Sprintf("%v", raw)
	return resolveTemplateString(value, vars)
}

// An absent key takes the default; a present but unparseable one is an error, so an edited
// manifest cannot have a value quietly ignored.
func resolveConfigBoolParam(
	params map[string]interface{},
	key string,
	defaultValue bool,
	vars map[string]string,
) (bool, error) {
	raw, ok := params[key]
	if !ok || raw == nil {
		return defaultValue, nil
	}
	switch typed := raw.(type) {
	case bool:
		return typed, nil
	case string:
		parsed, err := strconv.ParseBool(resolveTemplateString(typed, vars))
		if err != nil {
			return false, invalidParamError(key, raw, "a boolean")
		}
		return parsed, nil
	default:
		parsed, err := strconv.ParseBool(fmt.Sprintf("%v", raw))
		if err != nil {
			return false, invalidParamError(key, raw, "a boolean")
		}
		return parsed, nil
	}
}
func resolveConfigIntParam(
	params map[string]interface{},
	key string,
	defaultValue int,
	vars map[string]string,
) (int, error) {
	raw, ok := params[key]
	if !ok || raw == nil {
		return defaultValue, nil
	}
	switch typed := raw.(type) {
	case int:
		return typed, nil
	case int32:
		return int(typed), nil
	case int64:
		return int(typed), nil
	case float64:
		return int(typed), nil
	case string:
		parsed, err := strconv.Atoi(resolveTemplateString(typed, vars))
		if err != nil {
			return 0, invalidParamError(key, raw, "a whole number")
		}
		return parsed, nil
	default:
		parsed, err := strconv.Atoi(fmt.Sprintf("%v", raw))
		if err != nil {
			return 0, invalidParamError(key, raw, "a whole number")
		}
		return parsed, nil
	}
}
func resolveConfigInt64Param(
	params map[string]interface{},
	key string,
	defaultValue int64,
	vars map[string]string,
) (int64, error) {
	raw, ok := params[key]
	if !ok || raw == nil {
		return defaultValue, nil
	}
	switch typed := raw.(type) {
	case int:
		return int64(typed), nil
	case int32:
		return int64(typed), nil
	case int64:
		return typed, nil
	case float64:
		return int64(typed), nil
	case string:
		parsed, err := strconv.ParseInt(resolveTemplateString(typed, vars), 10, 64)
		if err != nil {
			return 0, invalidParamError(key, raw, "a whole number")
		}
		return parsed, nil
	default:
		parsed, err := strconv.ParseInt(fmt.Sprintf("%v", raw), 10, 64)
		if err != nil {
			return 0, invalidParamError(key, raw, "a whole number")
		}
		return parsed, nil
	}
}

// invalidParamError names the key, the value and what was expected.
func invalidParamError(key string, raw interface{}, want string) error {
	return fmt.Errorf("params.%s = %v is not %s", key, raw, want)
}
func resolveConfigStringSliceParam(
	params map[string]interface{},
	key string,
	vars map[string]string,
) []string {
	raw, ok := params[key]
	if !ok || raw == nil {
		return nil
	}

	switch typed := raw.(type) {
	case []string:
		out := make([]string, 0, len(typed))
		for _, item := range typed {
			resolved := resolveTemplateString(strings.TrimSpace(item), vars)
			if !util.IsEmptyString(resolved) {
				out = append(out, resolved)
			}
		}
		return out
	case []interface{}:
		out := make([]string, 0, len(typed))
		for _, item := range typed {
			value := resolveTemplateString(strings.TrimSpace(fmt.Sprintf("%v", item)), vars)
			if !util.IsEmptyString(value) {
				out = append(out, value)
			}
		}
		return out
	case string:
		// A scalar is one element, never a comma list. The keys read through this helper carry
		// SQL, yb-admin commands and file names, all of which legitimately contain commas, so
		// splitting turned a single valid value into several invalid ones. The singular "query"
		// key is read through resolveConfigStringParam and has always behaved this way.
		value := resolveTemplateString(strings.TrimSpace(typed), vars)
		if util.IsEmptyString(value) {
			return nil
		}
		return []string{value}
	default:
		value := resolveTemplateString(strings.TrimSpace(fmt.Sprintf("%v", raw)), vars)
		if util.IsEmptyString(value) {
			return nil
		}
		return []string{value}
	}
}
func resolveTemplateString(input string, vars map[string]string) string {
	resolved := input
	for i := 0; i < 5; i++ {
		previous := resolved
		for key, value := range vars {
			resolved = strings.ReplaceAll(resolved, "${"+key+"}", value)
		}
		if previous == resolved {
			break
		}
	}
	return resolved
}
func camelToSnake(input string) string {
	var b strings.Builder
	for i, r := range input {
		if i > 0 && r >= 'A' && r <= 'Z' {
			b.WriteByte('_')
		}
		b.WriteRune(r)
	}
	return strings.ToLower(b.String())
}
