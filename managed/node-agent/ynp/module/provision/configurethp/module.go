// Copyright (c) YugabyteDB, Inc.

package configurethp

import (
	"node-agent/ynp/config"
	"path/filepath"
)

const ModuleName = "ConfigureTHP"

type ConfigureTHP struct {
	*config.BaseModule
}

func NewConfigureTHP(basePath string) config.Module {
	return &ConfigureTHP{
		BaseModule: config.NewBaseModule(ModuleName, filepath.Join(basePath, "configure_thp")),
	}
}
