// Copyright (c) YugabyteDB, Inc.

package otelcol

import (
	"node-agent/ynp/config"
	"path/filepath"
)

const ModuleName = "ConfigureOtelcol"

// ConfigureOtelcol represents the Otelcol configuration module.
type ConfigureOtelcol struct {
	*config.BaseModule
}

func NewConfigureOtelcol(basePath string) config.Module {
	return &ConfigureOtelcol{
		BaseModule: config.NewBaseModule(ModuleName, filepath.Join(basePath, "otelcol")),
	}
}
