// Copyright (c) YugabyteDB, Inc.

package installpackages

import (
	"node-agent/ynp/config"
	"path/filepath"
)

const ModuleName = "InstallPackages"

type InstallPackages struct {
	*config.BaseModule
}

func NewInstallPackages(basePath string) config.Module {
	return &InstallPackages{
		BaseModule: config.NewBaseModule(ModuleName, filepath.Join(basePath, "install_packages")),
	}
}
