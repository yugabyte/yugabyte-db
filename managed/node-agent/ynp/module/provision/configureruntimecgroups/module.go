// Copyright (c) YugabyteDB, Inc.

package configureruntimecgroups

import (
	"node-agent/ynp/config"
	"path/filepath"
)

// ModuleName is the INI section / module id for runtime cgroup setup.
const ModuleName = "ConfigureRuntimeCgroups"

// ConfigureRuntimeCgroups configures host cgroups for YugabyteDB processes during YNP.
type ConfigureRuntimeCgroups struct {
	*config.BaseModule
}

// NewConfigureRuntimeCgroups builds the module rooted at configure_runtime_cgroups under provision.
func NewConfigureRuntimeCgroups(basePath string) config.Module {
	return &ConfigureRuntimeCgroups{
		BaseModule: config.NewBaseModule(
			ModuleName,
			filepath.Join(basePath, "configure_runtime_cgroups"),
		),
	}
}
