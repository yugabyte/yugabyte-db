// Copyright (c) YugabyteDB, Inc.

package pglogotelcol

import (
	"node-agent/ynp/config"
	"path/filepath"
)

const ModuleName = "ConfigurePgLogOtelcol"

// ConfigurePgLogOtelcol represents the PgLogOtelcol configuration module.
type ConfigurePgLogOtelcol struct {
	*config.BaseModule
}

func NewConfigurePgLogOtelcol(basePath string) config.Module {
	return &ConfigurePgLogOtelcol{
		BaseModule: config.NewBaseModule(ModuleName, filepath.Join(basePath, "pg_log_otelcol")),
	}
}
