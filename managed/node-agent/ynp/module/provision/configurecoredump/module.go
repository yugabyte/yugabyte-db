// Copyright (c) YugabyteDB, Inc.

package configurecoredump

import (
	"node-agent/ynp/config"
	"path/filepath"
)

const ModuleName = "ConfigureCoredump"

// ConfigureCoredump represents the coredump configuration module.
type ConfigureCoredump struct {
	*config.BaseModule
}

func NewConfigureCoredump(basePath string) config.Module {
	return &ConfigureCoredump{
		BaseModule: config.NewBaseModule(ModuleName, filepath.Join(basePath, "coredump")),
	}
}
