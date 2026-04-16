// Copyright (c) YugabyteDB, Inc.

package teleport

import (
	"node-agent/ynp/config"
	"path/filepath"
)

const ModuleName = "ConfigureTeleport"

// ConfigureTeleport represents the Teleport configuration module.
type ConfigureTeleport struct {
	*config.BaseModule
}

func NewConfigureTeleport(basePath string) config.Module {
	return &ConfigureTeleport{
		BaseModule: config.NewBaseModule(ModuleName, filepath.Join(basePath, "teleport")),
	}
}
