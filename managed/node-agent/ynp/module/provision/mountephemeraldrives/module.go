// Copyright (c) YugabyteDB, Inc.

package mountephemeraldrives

import (
	"node-agent/ynp/config"
	"path/filepath"
)

const ModuleName = "MountEphemeralDrive"

type MountEphemeralDrive struct {
	*config.BaseModule
}

func NewMountEphemeralDrive(basePath string) config.Module {
	return &MountEphemeralDrive{
		BaseModule: config.NewBaseModule(
			ModuleName,
			filepath.Join(basePath, "mount_ephemeral_drives"),
		),
	}
}
