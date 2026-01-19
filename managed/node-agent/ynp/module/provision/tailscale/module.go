// Copyright (c) YugabyteDB, Inc.

package tailscale

import (
	"node-agent/ynp/config"
	"path/filepath"
)

const ModuleName = "ConfigureTailscale"

// ConfigureTailscale represents the Tailscale configuration module.
type ConfigureTailscale struct {
	*config.BaseModule
}

func NewConfigureTailscale(basePath string) config.Module {
	return &ConfigureTailscale{
		BaseModule: config.NewBaseModule(ModuleName, filepath.Join(basePath, "tailscale")),
	}
}
