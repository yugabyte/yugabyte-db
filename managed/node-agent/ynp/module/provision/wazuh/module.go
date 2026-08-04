// Copyright (c) YugabyteDB, Inc.

package wazuh

import (
	"node-agent/ynp/config"
	"path/filepath"
)

const ModuleName = "ConfigureWazuh"

// ConfigureWazuh represents the Wazuh configuration module.
type ConfigureWazuh struct {
	*config.BaseModule
}

func NewConfigureWazuh(basePath string) config.Module {
	return &ConfigureWazuh{
		BaseModule: config.NewBaseModule(ModuleName, filepath.Join(basePath, "wazuh")),
	}
}
