/*
 * Copyright (c) YugabyteDB, Inc.
 */

package supportbundlev2

import (
	"fmt"
	"sort"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	ybav2client "github.com/yugabyte/platform-go-client/v2"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
)

func isYbaScriptComponent(cfg *supportBundleV2Config, componentName string) bool {
	componentCfg, ok := cfg.Spec.ComponentConfigs[componentName]
	if !ok || util.IsEmptyString(componentCfg.Script.Entrypoint) {
		return false
	}
	for _, name := range cfg.Spec.GlobalComponents["YBA"] {
		if strings.TrimSpace(name) == componentName {
			return true
		}
	}
	return false
}
func allComponentsAreYbaScript(cfg *supportBundleV2Config, components []string) bool {
	for _, componentName := range components {
		if !isYbaScriptComponent(cfg, componentName) {
			return false
		}
	}
	return len(components) > 0
}

// resolveLegacyGlobalComponent sends a v1 global under its own name: one listed outside the YBA
// group with no script, as the server's SupportBundleV2ManifestLoader.isV1GlobalComponent decides.
// There is no list here, so a v1 global the server knows needs only a manifest entry.
func resolveLegacyGlobalComponent(
	cfg *supportBundleV2Config,
	componentName string,
) (ybav2client.SupportBundleComponentType, bool) {
	if componentCfg, ok := cfg.Spec.ComponentConfigs[componentName]; ok &&
		!util.IsEmptyString(componentCfg.Script.Entrypoint) {
		return "", false
	}
	for groupName, components := range cfg.Spec.GlobalComponents {
		if groupName == "YBA" {
			continue
		}
		for _, name := range components {
			if strings.TrimSpace(name) == componentName {
				return ybav2client.SupportBundleComponentType(componentName), true
			}
		}
	}
	return "", false
}
func isComponentEnabled(cfg *supportBundleV2Config, componentName string) bool {
	componentCfg, ok := cfg.Spec.ComponentConfigs[componentName]
	if !ok {
		return true
	}
	return componentCfg.Enabled
}
func containsComponentType(
	values []ybav2client.SupportBundleComponentType,
	target ybav2client.SupportBundleComponentType,
) bool {
	for _, value := range values {
		if value == target {
			return true
		}
	}
	return false
}

// disabledNamedComponents returns components named outright on --components that the manifest has
// switched off. Group members are not reported: asking for a group accepts whatever it contains.
func disabledNamedComponents(cmd *cobra.Command, cfg *supportBundleV2Config) []string {
	override, err := cmd.Flags().GetString("components")
	if err != nil || util.IsEmptyString(override) {
		return nil
	}

	groups := mergeComponentGroupMaps(cfg.Spec.NodeComponents, cfg.Spec.GlobalComponents)
	var disabled []string
	seen := make(map[string]struct{})
	for _, raw := range strings.Split(override, ",") {
		token := strings.TrimSpace(raw)
		if util.IsEmptyString(token) {
			continue
		}
		if _, isGroup := groups[token]; isGroup {
			continue
		}
		if _, exists := seen[token]; exists {
			continue
		}
		seen[token] = struct{}{}
		if !isComponentEnabled(cfg, token) {
			disabled = append(disabled, token)
		}
	}

	return disabled
}

// warnDisabledNamedComponents reports what disabledNamedComponents found.
func warnDisabledNamedComponents(cmd *cobra.Command, cfg *supportBundleV2Config) {
	disabled := disabledNamedComponents(cmd, cfg)
	if len(disabled) == 0 {
		return
	}
	logrus.Warnf(
		"Not collecting %s: disabled in the manifest. Set enabled: true to include it.\n",
		strings.Join(disabled, ", "),
	)
}
func selectedComponents(
	cmd *cobra.Command,
	nodeLevelGroups map[string][]string,
	globalGroups map[string][]string,
) []string {
	mergedGroups := mergeComponentGroupMaps(nodeLevelGroups, globalGroups)
	expandedFromConfig := flattenComponentGroups(mergedGroups)

	override, err := cmd.Flags().GetString("components")
	if err != nil || util.IsEmptyString(override) {
		return expandedFromConfig
	}

	result := make([]string, 0)
	seen := make(map[string]struct{})
	appendUnique := func(values []string) {
		for _, value := range values {
			component := strings.TrimSpace(value)
			if util.IsEmptyString(component) {
				continue
			}
			if _, exists := seen[component]; exists {
				continue
			}
			seen[component] = struct{}{}
			result = append(result, component)
		}
	}

	for _, raw := range strings.Split(override, ",") {
		token := strings.TrimSpace(raw)
		if util.IsEmptyString(token) {
			continue
		}
		if groupedComponents, ok := mergedGroups[token]; ok {
			appendUnique(groupedComponents)
			continue
		}
		appendUnique([]string{token})
	}
	return result
}
func mergeComponentGroupMaps(nodeLevelGroups, globalGroups map[string][]string) map[string][]string {
	merged := make(map[string][]string)
	for name, components := range globalGroups {
		merged[name] = components
	}
	for name, components := range nodeLevelGroups {
		merged[name] = components
	}
	return merged
}
func flattenComponentGroups(groups map[string][]string) []string {
	if len(groups) == 0 {
		return nil
	}

	result := make([]string, 0)
	seen := make(map[string]struct{})
	appendUnique := func(values []string) {
		for _, value := range values {
			component := strings.TrimSpace(value)
			if util.IsEmptyString(component) {
				continue
			}
			if _, exists := seen[component]; exists {
				continue
			}
			seen[component] = struct{}{}
			result = append(result, component)
		}
	}

	// The manifest's own group keys are the list: there is nothing to enumerate in Go. Sorted
	// only so a run is reproducible - a group's position does not reach the request. Each group
	// feeds its own per-type spec list, within-list order follows the group's member list, and the
	// components array is sorted, so visiting Bash before Files or after changes nothing.
	groupNames := make([]string, 0, len(groups))
	for groupName := range groups {
		groupNames = append(groupNames, groupName)
	}
	sort.Strings(groupNames)
	for _, groupName := range groupNames {
		appendUnique(groups[groupName])
	}

	return result
}

// nodeGroupToComponentType maps a spec.NodeComponents group to the API spec list its components
// belong in, keyed by the five groups the manifest declares and the server's
// SupportBundleV2ManifestMerger reads. It stays hardcoded because it is not a naming convention but
// which executor runs the component: renaming a group needs the manifest to declare its type.
var nodeGroupToComponentType = map[string]string{
	"Files":   "FilesComponent",
	"Bash":    "BashComponent",
	"YSQL":    "YSQLComponent",
	"YCQL":    "YCQLComponent",
	"YbAdmin": "YbAdminComponent",
}

func resolveNodeComponentType(cfg *supportBundleV2Config, componentName string) (string, error) {
	for groupName, components := range cfg.Spec.NodeComponents {
		for _, name := range components {
			if strings.TrimSpace(name) == componentName {
				if componentType, ok := nodeGroupToComponentType[groupName]; ok {
					return componentType, nil
				}
			}
		}
	}
	return "", fmt.Errorf(
		"component %q is not listed under spec.NodeComponents; add it to one of: %s",
		componentName,
		strings.Join(runnableNodeGroupNames(cfg), ", "),
	)
}

// runnableNodeGroupNames lists the manifest's node groups that map to a component type, so the
// error above names the groups this manifest actually has rather than a fixed five.
func runnableNodeGroupNames(cfg *supportBundleV2Config) []string {
	names := make([]string, 0, len(cfg.Spec.NodeComponents))
	for groupName := range cfg.Spec.NodeComponents {
		if _, ok := nodeGroupToComponentType[groupName]; ok {
			names = append(names, groupName)
		}
	}
	sort.Strings(names)
	return names
}

// isNodeBashComponent reports whether the component runs through the bash executor, which is any
// node group mapping to BashComponent rather than the one spelled "Bash".
func isNodeBashComponent(cfg *supportBundleV2Config, componentName string) bool {
	for groupName, components := range cfg.Spec.NodeComponents {
		if nodeGroupToComponentType[groupName] != "BashComponent" {
			continue
		}
		for _, name := range components {
			if strings.TrimSpace(name) == componentName {
				return true
			}
		}
	}
	return false
}
func isFileListComponent(componentCfg supportBundleV2Component) bool {
	for _, key := range []string{fileListDirFlagKey, fileListFileFlagKey, fileListFilesKey} {
		if _, ok := componentCfg.Params[key]; !ok {
			return false
		}
	}
	return true
}
