// Copyright (c) YugabyteDB, Inc.

package updateos

import (
	"node-agent/ynp/config"
	"path/filepath"
)

const ModuleName = "Preprovision"

type Preprovision struct {
	*config.BaseModule
}

func NewPreprovision(basePath string) config.Module {
	return &Preprovision{
		BaseModule: config.NewBaseModule(ModuleName, filepath.Join(basePath, "update_os")),
	}
}
