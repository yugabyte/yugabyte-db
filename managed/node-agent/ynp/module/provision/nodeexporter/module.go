// Copyright (c) YugabyteDB, Inc.

package nodeexporter

import (
	"node-agent/ynp/config"
	"path/filepath"
)

const ModuleName = "ConfigureNodeExporter"

type ConfigureNodeExporter struct {
	*config.BaseModule
}

func NewConfigureNodeExporter(basePath string) config.Module {
	return &ConfigureNodeExporter{
		BaseModule: config.NewBaseModule(ModuleName, filepath.Join(basePath, "node_exporter")),
	}
}
