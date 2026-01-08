// Copyright (c) YugabyteDB, Inc.

package disablefirewalld

import (
	"node-agent/ynp/config"
	"path/filepath"
)

const ModuleName = "DisableFirewalld"

// DisableFirewalld represents the disabled firewalld configuration module.
type DisableFirewalld struct {
	*config.BaseModule
}

func NewDisableFirewalld(basePath string) config.Module {
	return &DisableFirewalld{
		BaseModule: config.NewBaseModule(ModuleName, filepath.Join(basePath, "disable_firewalld")),
	}
}
