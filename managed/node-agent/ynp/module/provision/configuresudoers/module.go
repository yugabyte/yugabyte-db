// Copyright (c) YugabyteDB, Inc.

package configuresudoers

import (
	"node-agent/ynp/config"
	"path/filepath"
)

const ModuleName = "ConfigureSudoers"

type ConfigureSudoers struct {
	*config.BaseModule
}

func NewConfigureSudoers(basePath string) config.Module {
	return &ConfigureSudoers{
		BaseModule: config.NewBaseModule(ModuleName, filepath.Join(basePath, "configure_sudoers")),
	}
}
