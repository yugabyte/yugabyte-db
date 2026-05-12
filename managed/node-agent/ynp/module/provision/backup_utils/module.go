// Copyright (c) YugabyteDB, Inc.

package backuputils

import (
	"node-agent/ynp/config"
	"path/filepath"
)

const ModuleName = "BackupUtils"

type BackupUtils struct {
	*config.BaseModule
}

func NewBackupUtils(basePath string) config.Module {
	return &BackupUtils{
		BaseModule: config.NewBaseModule("BackupUtils", filepath.Join(basePath, "backup_utils")),
	}
}
