// Copyright (c) YugabyteDB, Inc.

package chrony

import (
	"node-agent/ynp/config"
	"path/filepath"
)

const ModuleName = "ConfigureChrony"

type ConfigureChrony struct {
	*config.BaseModule
}

func NewConfigureChrony(basePath string) config.Module {
	return &ConfigureChrony{
		BaseModule: config.NewBaseModule(ModuleName, filepath.Join(basePath, "chrony")),
	}
}
