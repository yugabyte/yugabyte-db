// Copyright (c) YugabyteDB, Inc.

package configureos

import (
	"node-agent/ynp/config"
	"path/filepath"
)

const ModuleName = "ConfigureOs"

type ConfigureOs struct {
	*config.BaseModule
}

func NewConfigureOs(basePath string) config.Module {
	return &ConfigureOs{
		BaseModule: config.NewBaseModule(ModuleName, filepath.Join(basePath, "configure_os")),
	}
}
