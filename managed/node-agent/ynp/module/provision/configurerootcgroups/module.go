// Copyright (c) YugabyteDB, Inc.

package configurerootcgroups

import (
	"node-agent/ynp/config"
	"path/filepath"
)

// ModuleName is the INI section / module id for the root-only half of the cgroup setup.
const ModuleName = "ConfigureRootCgroups"

// ConfigureRootCgroups makes the host-level cgroup changes that need root. The yb user side is
// done by ConfigureRuntimeCgroups.
type ConfigureRootCgroups struct {
	*config.BaseModule
}

func NewConfigureRootCgroups(basePath string) config.Module {
	return &ConfigureRootCgroups{
		BaseModule: config.NewBaseModule(
			ModuleName,
			filepath.Join(basePath, "configure_root_cgroups"),
		),
	}
}
