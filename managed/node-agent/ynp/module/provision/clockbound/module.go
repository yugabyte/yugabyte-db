// Copyright (c) YugabyteDB, Inc.

package clockbound

import (
	"node-agent/ynp/config"
	"path/filepath"
)

const ModuleName = "ConfigureClockbound"

type ConfigureClockbound struct {
	*config.BaseModule
}

func NewConfigureClockbound(basePath string) config.Module {
	return &ConfigureClockbound{
		BaseModule: config.NewBaseModule(ModuleName, filepath.Join(basePath, "clockbound")),
	}
}
