// Copyright (c) YugabyteDB, Inc.

package network

import (
	"node-agent/ynp/config"
	"path/filepath"
)

const ModuleName = "ConfigureNetwork"

type ConfigureNetwork struct {
	*config.BaseModule
}

func NewConfigureNetwork(basePath string) config.Module {
	return &ConfigureNetwork{
		BaseModule: config.NewBaseModule(ModuleName, filepath.Join(basePath, "network")),
	}
}
