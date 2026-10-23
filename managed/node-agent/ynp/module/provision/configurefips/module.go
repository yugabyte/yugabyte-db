// Copyright (c) YugabyteDB, Inc.

package configurefips

import (
	"node-agent/ynp/config"
	"path/filepath"
)

const ModuleName = "ConfigureFips"

type ConfigureFips struct {
	*config.BaseModule
}

func NewConfigureFips(basePath string) config.Module {
	return &ConfigureFips{
		BaseModule: config.NewBaseModule(ModuleName, filepath.Join(basePath, "configure_fips")),
	}
}
