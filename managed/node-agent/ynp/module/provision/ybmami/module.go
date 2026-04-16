// Copyright (c) YugabyteDB, Inc.

package ybmami

import (
	"node-agent/ynp/config"
	"path/filepath"
)

const ModuleName = "ConfigureYBMAMI"

type ConfigureYBMAMI struct {
	*config.BaseModule
}

func NewConfigureYBMAMI(basePath string) config.Module {
	return &ConfigureYBMAMI{
		BaseModule: config.NewBaseModule(ModuleName, filepath.Join(basePath, "ybm_ami")),
	}
}
