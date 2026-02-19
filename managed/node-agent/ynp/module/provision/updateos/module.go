// Copyright (c) YugabyteDB, Inc.

package updateos

import (
	"node-agent/ynp/config"
	"path/filepath"
)

const ModuleName = "UpdateOS"

type UpdateOS struct {
	*config.BaseModule
}

func NewUpdateOS(basePath string) config.Module {
	return &UpdateOS{
		BaseModule: config.NewBaseModule(ModuleName, filepath.Join(basePath, "update_os")),
	}
}
