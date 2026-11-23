/*
 * Copyright (c) YugabyteDB, Inc.
 */

package supportbundlev2

import (
	"fmt"
	"os"

	"gopkg.in/yaml.v3"
)

type supportBundleV2Config struct {
	Spec supportBundleV2Spec `yaml:"spec"`
}
type supportBundleV2Spec struct {
	CollectionTimeRange supportBundleV2CollectionTimeRange  `yaml:"collectionTimeRange"`
	NodeSelection       supportBundleV2NodeSelection        `yaml:"nodeSelection"`
	GlobalComponents    map[string][]string                 `yaml:"GlobalComponents"`
	NodeComponents      map[string][]string                 `yaml:"NodeComponents"`
	Execution           supportBundleV2Execution            `yaml:"execution"`
	Defaults            supportBundleV2Defaults             `yaml:"defaults"`
	ComponentConfigs    map[string]supportBundleV2Component `yaml:"componentConfigs"`
	// RequestBuildTimeUTC is set when materializing a create request (not loaded from YAML).
	RequestBuildTimeUTC string `yaml:"-"`
}
type supportBundleV2CollectionTimeRange struct {
	StartDate string `yaml:"startDate"`
	EndDate   string `yaml:"endDate"`
}

// supportBundleV2NodeSelection restricts node-level collection to specific DB nodes. An empty
// list means every node in the universe.
type supportBundleV2NodeSelection struct {
	NodeNames []string `yaml:"nodeNames"`
}
type supportBundleV2Execution struct {
	LinuxUser   string `yaml:"linuxUser"`
	TimeoutSecs int64  `yaml:"timeoutSecs"`
}
type supportBundleV2Defaults struct {
	MountPath            string `yaml:"mountPath"`
	NodeDataDirTemplate  string `yaml:"nodeDataDirTemplate"`
	YbHomeDir            string `yaml:"ybHomeDir"`
	RemoteTmpDir         string `yaml:"remoteTmpDir"`
	NodeAgentInstallPath string `yaml:"nodeAgentInstallPath"`
	FilterPgAuditLogs    bool   `yaml:"filterPgAuditLogs"`
}
type supportBundleV2Component struct {
	Enabled bool                   `yaml:"enabled"`
	Script  supportBundleV2Script  `yaml:"script"`
	Params  map[string]interface{} `yaml:"params"`
}
type supportBundleV2Script struct {
	ScriptFile string `yaml:"scriptFile"`
	Entrypoint string `yaml:"entrypoint"`
}

func loadSupportBundleV2Config(path string) (*supportBundleV2Config, error) {
	content, err := os.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("read config failed: %w", err)
	}
	var cfg supportBundleV2Config
	if err := yaml.Unmarshal(content, &cfg); err != nil {
		return nil, fmt.Errorf("yaml unmarshal failed: %w", err)
	}
	normalizeGlobalComponents(&cfg)
	return &cfg, nil
}

// normalizeGlobalComponents expands shorthand global groups: a legacy group written with no members
// ("YbaMetadata:") takes its own name as the component name, the same as listing it. The YBA group
// is excluded - it always names its components, such as ApplicationLogs.
func normalizeGlobalComponents(cfg *supportBundleV2Config) {
	if cfg == nil || len(cfg.Spec.GlobalComponents) == 0 {
		return
	}
	for groupName, components := range cfg.Spec.GlobalComponents {
		if groupName == "YBA" {
			continue
		}
		if len(components) == 0 {
			cfg.Spec.GlobalComponents[groupName] = []string{groupName}
		}
	}
}
