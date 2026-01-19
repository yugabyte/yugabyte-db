// Copyright (c) YugabyteDB, Inc.

package ulimitsalma8

import (
	"node-agent/ynp/config"
	"path/filepath"
)

const ModuleName = "ConfigureUlimitsAlma8"

// ConfigureUlimitsAlma8 represents the Ulimits-Alma8 configuration module.
type ConfigureUlimitsAlma8 struct {
	*config.BaseModule
}

func NewConfigureUlimitsAlma8(basePath string) config.Module {
	return &ConfigureUlimitsAlma8{
		BaseModule: config.NewBaseModule(ModuleName, filepath.Join(basePath, "ulimits_alma8")),
	}
}
