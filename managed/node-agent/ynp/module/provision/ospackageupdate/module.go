// Copyright (c) YugabyteDB, Inc.

package ospackageupdate

import (
	"node-agent/ynp/config"
	"path/filepath"
)

const ModuleName = "ConfigureOSPackageUpdate"

// ConfigureOSPackageUpdate represents the OS package update configuration module.
type ConfigureOSPackageUpdate struct {
	*config.BaseModule
}

func NewConfigureOSPackageUpdate(basePath string) config.Module {
	return &ConfigureOSPackageUpdate{
		BaseModule: config.NewBaseModule(ModuleName, filepath.Join(basePath, "os_package_update")),
	}
}
