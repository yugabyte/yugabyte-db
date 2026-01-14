// Copyright (c) YugabyteDB, Inc.

package systemd

import (
	"node-agent/ynp/config"
	"path/filepath"
)

const ModuleName = "ConfigureSystemd"

type ConfigureSystemd struct {
	*config.BaseModule
}

func NewConfigureSystemd(basePath string) config.Module {
	return &ConfigureSystemd{
		BaseModule: config.NewBaseModule(ModuleName, filepath.Join(basePath, "systemd")),
	}
}
