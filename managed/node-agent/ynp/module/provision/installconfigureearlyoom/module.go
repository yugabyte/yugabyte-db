// Copyright (c) YugabyteDB, Inc.

package installconfigureearlyoom

import (
	"node-agent/ynp/config"
	"path/filepath"
)

const ModuleName = "InstallConfigureEarlyoom"

type InstallConfigureEarlyoom struct {
	*config.BaseModule
}

func NewInstallConfigureEarlyoom(basePath string) config.Module {
	return &InstallConfigureEarlyoom{
		BaseModule: config.NewBaseModule(
			ModuleName,
			filepath.Join(basePath, "install_configure_earlyoom"),
		),
	}
}
