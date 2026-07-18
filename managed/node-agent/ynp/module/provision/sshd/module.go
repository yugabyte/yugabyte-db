// Copyright (c) YugabyteDB, Inc.

package sshd

import (
	"node-agent/ynp/config"
	"path/filepath"
)

const ModuleName = "ConfigureSshD"

type ConfigureSshD struct {
	*config.BaseModule
}

func NewConfigureSshD(basePath string) config.Module {
	return &ConfigureSshD{
		BaseModule: config.NewBaseModule(ModuleName, filepath.Join(basePath, "sshd")),
	}
}
