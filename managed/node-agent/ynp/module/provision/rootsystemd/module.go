// Copyright (c) YugabyteDB, Inc.

package rootsystemd

import (
	"node-agent/ynp/config"
	"path/filepath"
)

const ModuleName = "ConfigureRootSystemd"

type ConfigureRootSystemd struct {
	*config.BaseModule
}

func NewConfigureRootSystemd(basePath string) config.Module {
	return &ConfigureRootSystemd{
		BaseModule: config.NewBaseModule(ModuleName, filepath.Join(basePath, "rootsystemd")),
	}
}
